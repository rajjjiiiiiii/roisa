// ROIVolume.cpp — Core data model implementation

#include "ROIVolume.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

// ITK I/O
#include <itkCastImageFilter.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImageFileReader.h>
#include <itkImageFileWriter.h>
#include <itkImageSeriesReader.h>

// ITK processing
#include <itkIdentityTransform.h>
#include <itkLinearInterpolateImageFunction.h>
#include <itkNearestNeighborInterpolateImageFunction.h>
#include <itkResampleImageFilter.h>
#include <itkSignedMaurerDistanceMapImageFilter.h>
#include <itkTransformFileReader.h>
#include <itkTransformFileWriter.h>

// ITK registration
#include <itkCenteredTransformInitializer.h>
#include <itkEuler3DTransform.h>
#include <itkAffineTransform.h>
#include <itkBSplineTransform.h>
#include <itkBSplineTransformInitializer.h>
#include <itkImageRegistrationMethodv4.h>
#include <itkMeanSquaresImageToImageMetricv4.h>
#include <itkMattesMutualInformationImageToImageMetricv4.h>
#include <itkRegularStepGradientDescentOptimizerv4.h>
#include <itkRegistrationParameterScalesFromPhysicalShift.h>
#include <itkCompositeTransform.h>

namespace fs = std::filesystem;

// ── Dimension helpers ─────────────────────────────────────────────────────────

int ROIVolume::nx() const
{
    if (!m_displayImg) return 0;
    return static_cast<int>(m_displayImg->GetLargestPossibleRegion().GetSize()[0]);
}

int ROIVolume::ny() const
{
    if (!m_displayImg) return 0;
    return static_cast<int>(m_displayImg->GetLargestPossibleRegion().GetSize()[1]);
}

int ROIVolume::nz() const
{
    if (!m_displayImg) return 0;
    return static_cast<int>(m_displayImg->GetLargestPossibleRegion().GetSize()[2]);
}

// ── Load ──────────────────────────────────────────────────────────────────────

bool ROIVolume::load(const std::string& path, const std::string& seriesUID)
{
    try {
        FloatPtr raw;
        if (fs::is_directory(path))
            raw = loadDicomSeries(path, seriesUID);
        else {
            m_firstDicomFile.clear();
            raw = loadNiftiOrMeta(path);
        }

        if (!raw) return false;

        m_origImg    = raw;
        m_displayImg = resampleIsotropic(raw, TARGET_SIZE);
        m_mask       = createMask(m_displayImg);
        m_history.clear();
        computeWindow();

        auto sz = m_displayImg->GetLargestPossibleRegion().GetSize();
        auto sp = m_displayImg->GetSpacing();
        std::cout << "  Loaded: display size = "
                  << sz[0] << " x " << sz[1] << " x " << sz[2]
                  << "  spacing = " << sp[0] << " mm (isotropic)\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "ROIVolume::load error: " << e.what() << "\n";
        return false;
    }
}

ROIVolume::FloatPtr ROIVolume::loadNiftiOrMeta(const std::string& path)
{
    using ReaderType = itk::ImageFileReader<FloatImage3>;
    auto reader = ReaderType::New();
    reader->SetFileName(path);
    reader->Update();
    return reader->GetOutput();
}

ROIVolume::FloatPtr ROIVolume::loadDicomSeries(const std::string& dir,
                                               const std::string& uid)
{
    auto nameGen = itk::GDCMSeriesFileNames::New();
    nameGen->SetUseSeriesDetails(true);
    nameGen->SetDirectory(dir);

    const auto& seriesIds = nameGen->GetSeriesUIDs();
    if (seriesIds.empty())
        throw std::runtime_error("No DICOM series found in: " + dir);

    // Pick the requested UID; fall back to first series if not found
    std::string chosenUID = uid;
    if (chosenUID.empty() ||
        std::find(seriesIds.begin(), seriesIds.end(), chosenUID) == seriesIds.end())
        chosenUID = seriesIds.front();

    auto fileNames = nameGen->GetFileNames(chosenUID);
    m_firstDicomFile = fileNames.empty() ? "" : fileNames.front();
    std::cout << "  DICOM: series " << chosenUID
              << "  (" << fileNames.size() << " slices)\n";

    // Read as short, then cast to float
    using ShortImage = itk::Image<short, 3>;
    using SeriesReader = itk::ImageSeriesReader<ShortImage>;
    auto reader = SeriesReader::New();
    reader->SetFileNames(fileNames);
    auto gdcmIO = itk::GDCMImageIO::New();
    reader->SetImageIO(gdcmIO);
    reader->Update();

    using CastFilter = itk::CastImageFilter<ShortImage, FloatImage3>;
    auto cast = CastFilter::New();
    cast->SetInput(reader->GetOutput());
    cast->Update();
    return cast->GetOutput();
}

// ── Save mask ─────────────────────────────────────────────────────────────────

bool ROIVolume::saveMask(const std::string& outPath) const
{
    if (!m_origImg || !m_mask) return false;
    try {
        // Resample mask to original image space (nearest-neighbour)
        using CastFloat = itk::CastImageFilter<Int16Image3, FloatImage3>;
        auto castFwd = CastFloat::New();
        castFwd->SetInput(m_mask);
        castFwd->Update();

        using NNInterp = itk::NearestNeighborInterpolateImageFunction<FloatImage3, double>;
        using ResampleF = itk::ResampleImageFilter<FloatImage3, FloatImage3>;
        auto resample = ResampleF::New();
        resample->SetInput(castFwd->GetOutput());
        resample->SetReferenceImage(m_origImg);
        resample->UseReferenceImageOn();
        resample->SetInterpolator(NNInterp::New());
        resample->SetDefaultPixelValue(0.0f);
        resample->Update();

        // Cast back to int16 and save
        using CastInt16 = itk::CastImageFilter<FloatImage3, Int16Image3>;
        auto castBack = CastInt16::New();
        castBack->SetInput(resample->GetOutput());
        castBack->Update();

        using WriterType = itk::ImageFileWriter<Int16Image3>;
        auto writer = WriterType::New();
        writer->SetFileName(outPath);
        writer->SetInput(castBack->GetOutput());
        writer->Update();

        std::cout << "  Saved mask -> " << outPath << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "ROIVolume::saveMask error: " << e.what() << "\n";
        return false;
    }
}

// ── Load mask ─────────────────────────────────────────────────────────────────

bool ROIVolume::loadMask(const std::string& maskPath)
{
    if (!m_displayImg) return false;
    try {
        // Read as int16
        using ReaderType = itk::ImageFileReader<Int16Image3>;
        auto reader = ReaderType::New();
        reader->SetFileName(maskPath);
        reader->Update();

        // Resample to display grid (nearest-neighbour)
        Int16Ptr disp = resampleMaskToRef(reader->GetOutput(), m_displayImg);

        // Record undo for every voxel that will change
        {
            int total = nx() * ny() * nz();
            const int16_t* oldBuf = m_mask->GetBufferPointer();
            const int16_t* newBuf = disp->GetBufferPointer();
            std::vector<std::array<int,3>> idxs;
            std::vector<int16_t>           olds;
            for (int z = 0; z < nz(); ++z)
              for (int y = 0; y < ny(); ++y)
                for (int x = 0; x < nx(); ++x) {
                    int lin = x + nx()*y + nx()*ny()*z;
                    if (oldBuf[lin] != newBuf[lin]) {
                        idxs.push_back({x, y, z});
                        olds.push_back(oldBuf[lin]);
                    }
                }
            if (!idxs.empty())
                pushUndo(std::move(idxs), std::move(olds));
        }

        replaceMask(disp);
        std::cout << "  Loaded mask: " << maskPath << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "ROIVolume::loadMask error: " << e.what() << "\n";
        return false;
    }
}

// ── Resampling helpers ────────────────────────────────────────────────────────

ROIVolume::FloatPtr ROIVolume::resampleIsotropic(FloatPtr img, int targetSize)
{
    auto size    = img->GetLargestPossibleRegion().GetSize();
    auto spacing = img->GetSpacing();

    double extX  = size[0] * spacing[0];
    double extY  = size[1] * spacing[1];
    double extZ  = size[2] * spacing[2];
    double maxExt = std::max({extX, extY, extZ});
    double newSp  = maxExt / targetSize;

    FloatImage3::SizeType newSize;
    newSize[0] = std::max(1LL, static_cast<long long>(std::round(extX / newSp)));
    newSize[1] = std::max(1LL, static_cast<long long>(std::round(extY / newSp)));
    newSize[2] = std::max(1LL, static_cast<long long>(std::round(extZ / newSp)));

    FloatImage3::SpacingType newSpacing;
    newSpacing.Fill(static_cast<float>(newSp));

    using LinearInterp = itk::LinearInterpolateImageFunction<FloatImage3, double>;
    using IdentityTx   = itk::IdentityTransform<double, 3>;
    using Resample     = itk::ResampleImageFilter<FloatImage3, FloatImage3>;

    auto resample = Resample::New();
    resample->SetInput(img);
    resample->SetTransform(IdentityTx::New());
    resample->SetInterpolator(LinearInterp::New());
    resample->SetOutputOrigin(img->GetOrigin());
    resample->SetOutputSpacing(newSpacing);
    resample->SetOutputDirection(img->GetDirection());
    resample->SetSize(newSize);
    resample->SetDefaultPixelValue(0.0f);
    resample->Update();
    return resample->GetOutput();
}

// ── Public isotropic resampling ───────────────────────────────────────────────

bool ROIVolume::resampleToIsotropicSpacing(float spacingMm)
{
    if (!m_origImg) return false;
    try {
        auto origSpacing = m_origImg->GetSpacing();
        auto origSize    = m_origImg->GetLargestPossibleRegion().GetSize();

        if (spacingMm <= 0.f) {
            spacingMm = static_cast<float>(
                std::min({origSpacing[0], origSpacing[1], origSpacing[2]}));
            if (spacingMm <= 0.f) spacingMm = 1.f;
        }

        FloatImage3::SpacingType newSpacing;
        newSpacing.Fill(spacingMm);

        FloatImage3::SizeType newSize;
        for (unsigned i = 0; i < 3; ++i)
            newSize[i] = static_cast<itk::SizeValueType>(
                std::max(1.0, std::ceil(origSize[i] * origSpacing[i] / spacingMm)));

        using LinearInterp = itk::LinearInterpolateImageFunction<FloatImage3, double>;
        using IdentityTx   = itk::IdentityTransform<double, 3>;
        using Resample     = itk::ResampleImageFilter<FloatImage3, FloatImage3>;

        auto resample = Resample::New();
        resample->SetInput(m_origImg);
        resample->SetTransform(IdentityTx::New());
        resample->SetInterpolator(LinearInterp::New());
        resample->SetOutputOrigin(m_origImg->GetOrigin());
        resample->SetOutputSpacing(newSpacing);
        resample->SetOutputDirection(m_origImg->GetDirection());
        resample->SetSize(newSize);
        resample->SetDefaultPixelValue(0.0f);
        resample->Update();

        m_displayImg = resample->GetOutput();
        m_mask       = createMask(m_displayImg);
        m_history.clear();
        computeWindow();

        auto sz = m_displayImg->GetLargestPossibleRegion().GetSize();
        std::cout << "  Resampled to isotropic " << spacingMm << " mm: "
                  << sz[0] << " x " << sz[1] << " x " << sz[2] << "\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "ROIVolume::resampleToIsotropicSpacing error: " << e.what() << "\n";
        return false;
    }
}

ROIVolume::Int16Ptr ROIVolume::createMask(FloatPtr ref)
{
    auto mask = Int16Image3::New();
    mask->SetRegions(ref->GetLargestPossibleRegion());
    mask->SetSpacing(ref->GetSpacing());
    mask->SetOrigin(ref->GetOrigin());
    mask->SetDirection(ref->GetDirection());
    mask->Allocate(true);   // zero-fill
    return mask;
}

ROIVolume::Int16Ptr ROIVolume::resampleMaskToRef(Int16Ptr mask, FloatPtr ref)
{
    // Cast int16 -> float, resample with NN, cast back
    using CastFwd  = itk::CastImageFilter<Int16Image3, FloatImage3>;
    using NNInterp = itk::NearestNeighborInterpolateImageFunction<FloatImage3, double>;
    using ResampleF = itk::ResampleImageFilter<FloatImage3, FloatImage3>;
    using CastBack = itk::CastImageFilter<FloatImage3, Int16Image3>;

    auto cf = CastFwd::New();  cf->SetInput(mask);  cf->Update();

    auto r = ResampleF::New();
    r->SetInput(cf->GetOutput());
    r->SetReferenceImage(ref);
    r->UseReferenceImageOn();
    r->SetInterpolator(NNInterp::New());
    r->SetDefaultPixelValue(0.0f);
    r->Update();

    auto cb = CastBack::New();  cb->SetInput(r->GetOutput());  cb->Update();
    return cb->GetOutput();
}

// ── Window ────────────────────────────────────────────────────────────────────

void ROIVolume::computeWindow()
{
    if (!m_displayImg) return;
    const float* buf = m_displayImg->GetBufferPointer();
    int n = static_cast<int>(m_displayImg->GetLargestPossibleRegion().GetNumberOfPixels());
    std::vector<float> sorted(buf, buf + n);
    std::sort(sorted.begin(), sorted.end());
    m_vmin = sorted[static_cast<size_t>(0.01 * n)];
    m_vmax = sorted[static_cast<size_t>(0.99 * n)];
}

// ── Voxel access ──────────────────────────────────────────────────────────────

static inline int linearIdx(int x, int y, int z, int nx, int ny)
{
    return x + nx * y + nx * ny * z;
}

float ROIVolume::getIntensity(int x, int y, int z) const
{
    if (!m_displayImg) return 0.f;
    const float* buf = m_displayImg->GetBufferPointer();
    return buf[linearIdx(x, y, z, nx(), ny())];
}

int16_t ROIVolume::getMaskLabel(int x, int y, int z) const
{
    if (!m_mask) return 0;
    const int16_t* buf = m_mask->GetBufferPointer();
    return buf[linearIdx(x, y, z, nx(), ny())];
}

void ROIVolume::setMaskLabel(int x, int y, int z, int16_t label)
{
    if (!m_mask) return;
    int16_t* buf = m_mask->GetBufferPointer();
    buf[linearIdx(x, y, z, nx(), ny())] = label;
}

// ── Slice data ────────────────────────────────────────────────────────────────

int ROIVolume::sliceRows(int axis) const
{
    switch (axis) {
        case 0: return ny();   // sagittal: rows = y
        case 1: return nx();   // coronal:  rows = x
        case 2: return nx();   // axial:    rows = x
        default: return 0;
    }
}

int ROIVolume::sliceCols(int axis) const
{
    switch (axis) {
        case 0: return nz();   // sagittal: cols = z
        case 1: return nz();   // coronal:  cols = z
        case 2: return ny();   // axial:    cols = y
        default: return 0;
    }
}

void ROIVolume::sliceFromBuffer(const float* buf, int NX, int NY, int NZ,
                                 int axis, int idx, std::vector<float>& dst)
{
    if (!buf) { dst.clear(); return; }
    // Slice dims mirror sliceRows/sliceCols:
    //   axis 0 (sag): rows=NY cols=NZ   axis 1 (cor): rows=NX cols=NZ
    //   axis 2 (axi): rows=NX cols=NY
    int rows = (axis == 0) ? NY : NX;
    int cols = (axis == 2) ? NY : NZ;
    dst.resize(rows * cols);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int lin = 0;
            if (axis == 0)       lin = linearIdx(idx, row, col, NX, NY);
            else if (axis == 1)  lin = linearIdx(row, idx, col, NX, NY);
            else                 lin = linearIdx(row, col, idx, NX, NY);
            dst[row * cols + col] = buf[lin];
        }
    }
}

void ROIVolume::sliceFromBufferU8(const uint8_t* buf, int NX, int NY, int NZ,
                                   int axis, int idx, std::vector<uint8_t>& dst)
{
    if (!buf) { dst.clear(); return; }
    int rows = (axis == 0) ? NY : NX;
    int cols = (axis == 2) ? NY : NZ;
    dst.resize(rows * cols);
    for (int row = 0; row < rows; ++row)
    for (int col = 0; col < cols; ++col) {
        int lin = (axis == 0) ? linearIdx(idx, row, col, NX, NY)
                : (axis == 1) ? linearIdx(row, idx, col, NX, NY)
                              : linearIdx(row, col, idx, NX, NY);
        dst[row * cols + col] = buf[lin];
    }
}

void ROIVolume::getIntensitySlice(int axis, int idx,
                                   std::vector<float>& dst) const
{
    if (!m_displayImg) return;
    sliceFromBuffer(m_displayImg->GetBufferPointer(),
                    nx(), ny(), nz(), axis, idx, dst);
}

void ROIVolume::getImageSliceProj(int axis, int idx, int mode, int slab,
                                   std::vector<float>& dst) const
{
    if (!m_displayImg) return;
    if (mode == 0) { getIntensitySlice(axis, idx, dst); return; }
    const float* buf = m_displayImg->GetBufferPointer();
    const int NX = nx(), NY = ny(), NZ = nz();
    const int rows = sliceRows(axis), cols = sliceCols(axis);
    dst.resize(rows * cols);

    const int pn = (axis == 0) ? NX : (axis == 1) ? NY : NZ;
    int lo = 0, hi = pn;
    if (slab > 0) { lo = std::max(0, idx - slab); hi = std::min(pn, idx + slab + 1); }

    for (int row = 0; row < rows; ++row)
    for (int col = 0; col < cols; ++col) {
        float acc = (mode == 2) ?  std::numeric_limits<float>::max()
                                : -std::numeric_limits<float>::max();
        for (int k = lo; k < hi; ++k) {
            int lin = (axis == 0) ? linearIdx(k, row, col, NX, NY)
                    : (axis == 1) ? linearIdx(row, k, col, NX, NY)
                                  : linearIdx(row, col, k, NX, NY);
            const float v = buf[lin];
            acc = (mode == 2) ? std::min(acc, v) : std::max(acc, v);
        }
        dst[row * cols + col] = acc;
    }
}

void ROIVolume::getMaskSlice(int axis, int idx,
                              std::vector<int16_t>& dst) const
{
    if (!m_mask) return;
    const int16_t* buf = m_mask->GetBufferPointer();
    int NX = nx(), NY = ny(), NZ = nz();
    int rows = sliceRows(axis), cols = sliceCols(axis);
    dst.resize(rows * cols);

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int lin = 0;
            if (axis == 0)      lin = linearIdx(idx, row, col, NX, NY);
            else if (axis == 1) lin = linearIdx(row, idx, col, NX, NY);
            else                lin = linearIdx(row, col, idx, NX, NY);
            dst[row * cols + col] = buf[lin];
        }
    }
}

// ── Fusion: resample this display image onto another volume's grid ─────────────

ROIVolume::FloatPtr ROIVolume::resampleDisplayTo(const ROIVolume* ref) const
{
    if (!m_displayImg || !ref || !ref->m_displayImg) return nullptr;

    FloatPtr refImg = ref->m_displayImg;

    // Fast path: identical grid → reuse as-is
    auto a = m_displayImg->GetLargestPossibleRegion().GetSize();
    auto b = refImg->GetLargestPossibleRegion().GetSize();
    if (a == b
        && m_displayImg->GetSpacing() == refImg->GetSpacing()
        && m_displayImg->GetOrigin()  == refImg->GetOrigin())
        return m_displayImg;

    using LinearInterp = itk::LinearInterpolateImageFunction<FloatImage3, double>;
    using IdentityTx   = itk::IdentityTransform<double, 3>;
    using Resample     = itk::ResampleImageFilter<FloatImage3, FloatImage3>;

    auto resample = Resample::New();
    resample->SetInput(m_displayImg);
    resample->SetTransform(IdentityTx::New());
    resample->SetInterpolator(LinearInterp::New());
    resample->SetOutputOrigin(refImg->GetOrigin());
    resample->SetOutputSpacing(refImg->GetSpacing());
    resample->SetOutputDirection(refImg->GetDirection());
    resample->SetSize(refImg->GetLargestPossibleRegion().GetSize());
    resample->SetDefaultPixelValue(0.0f);
    try {
        resample->Update();
    } catch (const std::exception& e) {
        std::cerr << "[resampleDisplayTo] " << e.what() << std::endl;
        return nullptr;
    }
    return resample->GetOutput();
}

// ── Mask bulk write ───────────────────────────────────────────────────────────

void ROIVolume::replaceMask(Int16Ptr newMask)
{
    m_mask = newMask;
    notifyChange();
}

// ── Clear ─────────────────────────────────────────────────────────────────────

bool ROIVolume::saveMaskRaw(const std::string& path) const
{
    if (!m_mask) return false;
    try {
        auto w = itk::ImageFileWriter<Int16Image3>::New();
        w->SetFileName(path);
        w->SetInput(m_mask);
        w->Update();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "saveMaskRaw error: " << e.what() << "\n";
        return false;
    }
}

bool ROIVolume::loadMaskRaw(const std::string& path)
{
    if (!m_displayImg) return false;
    try {
        auto r = itk::ImageFileReader<Int16Image3>::New();
        r->SetFileName(path);
        r->Update();
        Int16Ptr m = r->GetOutput();
        if (m->GetLargestPossibleRegion().GetSize()
            == m_displayImg->GetLargestPossibleRegion().GetSize())
            m_mask = m;
        else
            m_mask = resampleMaskToRef(m, m_displayImg);
        notifyChange();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "loadMaskRaw error: " << e.what() << "\n";
        return false;
    }
}

std::vector<int> ROIVolume::presentLabels() const
{
    std::vector<int> out;
    if (!m_mask) return out;
    const int16_t* buf = m_mask->GetBufferPointer();
    const long n = static_cast<long>(m_mask->GetLargestPossibleRegion().GetNumberOfPixels());
    std::set<int> seen;
    for (long i = 0; i < n; ++i) if (buf[i] > 0) seen.insert(buf[i]);
    out.assign(seen.begin(), seen.end());
    return out;
}

bool ROIVolume::saveLabelBinary(int label, const std::string& path) const
{
    if (!m_mask) return false;
    try {
        auto bin = Uint8Image3::New();
        bin->SetRegions(m_mask->GetLargestPossibleRegion());
        bin->SetSpacing(m_mask->GetSpacing());
        bin->SetOrigin(m_mask->GetOrigin());
        bin->SetDirection(m_mask->GetDirection());
        bin->Allocate();
        const int16_t* src = m_mask->GetBufferPointer();
        uint8_t* dst = bin->GetBufferPointer();
        const long n = static_cast<long>(m_mask->GetLargestPossibleRegion().GetNumberOfPixels());
        for (long i = 0; i < n; ++i) dst[i] = (src[i] == label) ? 1 : 0;

        using Writer = itk::ImageFileWriter<Uint8Image3>;
        auto w = Writer::New();
        w->SetFileName(path);
        w->SetInput(bin);
        w->Update();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "saveLabelBinary error: " << e.what() << "\n";
        return false;
    }
}

int ROIVolume::interpolateLabel(int label, int axis)
{
    if (!m_mask) return 0;
    const int NX = nx(), NY = ny(), NZ = nz();
    const int rows = sliceRows(axis), cols = sliceCols(axis);
    const int n    = (axis == 0) ? NX : (axis == 1) ? NY : NZ;
    int16_t* mbuf  = m_mask->GetBufferPointer();

    auto lin = [&](int row, int col, int idx) -> long {
        if (axis == 0) return linearIdx(idx, row, col, NX, NY);
        if (axis == 1) return linearIdx(row, idx, col, NX, NY);
        return linearIdx(row, col, idx, NX, NY);
    };

    using Img2D  = itk::Image<unsigned char, 2>;
    using Dist2D = itk::Image<float, 2>;
    using Maurer = itk::SignedMaurerDistanceMapImageFilter<Img2D, Dist2D>;

    auto makeSDF = [&](int idx, std::vector<float>& out) -> bool {
        auto img = Img2D::New();
        Img2D::SizeType sz; sz[0] = cols; sz[1] = rows;
        Img2D::RegionType reg; reg.SetSize(sz);
        img->SetRegions(reg); img->Allocate(); img->FillBuffer(0);
        unsigned char* ib = img->GetBufferPointer();
        bool any = false;
        for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if (mbuf[lin(r, c, idx)] == label) { ib[r*cols + c] = 1; any = true; }
        if (!any) return false;
        auto f = Maurer::New();
        f->SetInput(img);
        f->SetUseImageSpacing(false);
        f->SetSquaredDistance(false);
        f->Update();
        const float* db = f->GetOutput()->GetBufferPointer();
        out.assign(db, db + rows*cols);
        return true;
    };

    std::vector<int> pres;
    for (int i = 0; i < n; ++i) {
        bool any = false;
        for (int r = 0; r < rows && !any; ++r)
        for (int c = 0; c < cols && !any; ++c)
            if (mbuf[lin(r, c, i)] == label) any = true;
        if (any) pres.push_back(i);
    }
    if (pres.size() < 2) return 0;

    int filled = 0;
    for (size_t k = 0; k + 1 < pres.size(); ++k) {
        int a = pres[k], b = pres[k+1];
        if (b - a <= 1) continue;
        std::vector<float> sa, sb;
        if (!makeSDF(a, sa) || !makeSDF(b, sb)) continue;
        for (int m = a + 1; m < b; ++m) {
            const double t = (double)(m - a) / (b - a);
            bool wrote = false;
            for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const double blend = (1 - t) * sa[r*cols + c] + t * sb[r*cols + c];
                if (blend < 0) { mbuf[lin(r, c, m)] = (int16_t)label; wrote = true; }
            }
            if (wrote) ++filled;
        }
    }
    notifyChange();
    return filled;
}

void ROIVolume::clearLabel(int label)
{
    if (!m_mask) return;
    int16_t* buf = m_mask->GetBufferPointer();
    int n = static_cast<int>(m_mask->GetLargestPossibleRegion().GetNumberOfPixels());
    int cleared = 0;

    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t>           olds;
    int NX = nx(), NY = ny(), NZ = nz();

    for (int z = 0; z < NZ; ++z)
      for (int y = 0; y < NY; ++y)
        for (int x = 0; x < NX; ++x) {
            int lin = linearIdx(x, y, z, NX, NY);
            if (label < 0 ? buf[lin] != 0 : buf[lin] == label) {
                idxs.push_back({x, y, z});
                olds.push_back(buf[lin]);
                ++cleared;
            }
        }

    if (!idxs.empty()) {
        pushUndo(std::move(idxs), std::move(olds));
        buf = m_mask->GetBufferPointer();   // re-acquire after potential move
        if (label < 0) {
            std::fill(buf, buf + n, int16_t{0});
        } else {
            for (int z = 0; z < NZ; ++z)
              for (int y = 0; y < NY; ++y)
                for (int x = 0; x < NX; ++x)
                    if (buf[linearIdx(x, y, z, NX, NY)] == label)
                        buf[linearIdx(x, y, z, NX, NY)] = 0;
        }
        std::cout << "  Cleared " << cleared << " voxels"
                  << (label < 0 ? " (all labels)\n" : " of label " + std::to_string(label) + "\n");
        notifyChange();
    }
}

// ── Undo ──────────────────────────────────────────────────────────────────────

void ROIVolume::pushUndo(std::vector<std::array<int,3>> indices,
                          std::vector<int16_t>           oldValues)
{
    if (m_history.size() >= MAX_UNDO) m_history.pop_front();
    m_history.push_back({std::move(indices), std::move(oldValues)});
}

void ROIVolume::pushUndoForLabel(int label)
{
    if (!m_mask) return;
    int16_t* buf = m_mask->GetBufferPointer();
    int NX = nx(), NY = ny(), NZ = nz();
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t>           olds;
    for (int z = 0; z < NZ; ++z)
      for (int y = 0; y < NY; ++y)
        for (int x = 0; x < NX; ++x) {
            int16_t v = buf[linearIdx(x, y, z, NX, NY)];
            if (v == label) { idxs.push_back({x,y,z}); olds.push_back(v); }
        }
    if (!idxs.empty()) pushUndo(std::move(idxs), std::move(olds));
}

void ROIVolume::pushUndoAll()
{
    if (!m_mask) return;
    int16_t* buf = m_mask->GetBufferPointer();
    int NX = nx(), NY = ny(), NZ = nz();
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t>           olds;
    idxs.reserve(NX * NY * NZ);
    olds.reserve(NX * NY * NZ);
    for (int z = 0; z < NZ; ++z)
      for (int y = 0; y < NY; ++y)
        for (int x = 0; x < NX; ++x) {
            idxs.push_back({x, y, z});
            olds.push_back(buf[linearIdx(x, y, z, NX, NY)]);
        }
    pushUndo(std::move(idxs), std::move(olds));
}

bool ROIVolume::undo()
{
    if (m_history.empty()) { std::cout << "  Nothing to undo.\n"; return false; }
    auto entry = std::move(m_history.back());
    m_history.pop_back();

    int16_t* buf = m_mask->GetBufferPointer();
    int NX = nx(), NY = ny();
    for (size_t i = 0; i < entry.indices.size(); ++i) {
        auto [x, y, z] = entry.indices[i];
        buf[linearIdx(x, y, z, NX, NY)] = entry.oldValues[i];
    }
    std::cout << "  Undone (" << entry.indices.size() << " voxels restored)\n";
    notifyChange();
    return true;
}

// ── Notify ────────────────────────────────────────────────────────────────────

void ROIVolume::notifyChange()
{
    // Suppressed during background work so algorithms don't fire GUI updates
    // from a worker thread; the caller refreshes on the GUI thread afterwards.
    if (m_notifyEnabled && m_onChange) m_onChange();
}

// ── Reset window ──────────────────────────────────────────────────────────────

void ROIVolume::resetWindow()
{
    computeWindow();
    notifyChange();
}

// ── Spacing ───────────────────────────────────────────────────────────────────

double ROIVolume::voxelSpacingMm() const
{
    if (!m_displayImg) return 1.0;
    return m_displayImg->GetSpacing()[0];
}

// ── Label statistics ──────────────────────────────────────────────────────────

ROIVolume::LabelStats ROIVolume::computeStats(int label) const
{
    LabelStats s;
    s.label = label;
    if (!m_displayImg || !m_mask) return s;

    double sp = voxelSpacingMm();
    const float*   ibuf = m_displayImg->GetBufferPointer();
    const int16_t* mbuf = m_mask->GetBufferPointer();
    int NX = nx(), NY = ny(), NZ = nz();

    s.bboxX0 = NX; s.bboxY0 = NY; s.bboxZ0 = NZ;
    s.bboxX1 = -1; s.bboxY1 = -1; s.bboxZ1 = -1;

    double sumI = 0, sumI2 = 0;
    for (int z = 0; z < NZ; ++z)
      for (int y = 0; y < NY; ++y)
        for (int x = 0; x < NX; ++x) {
            int lin = linearIdx(x, y, z, NX, NY);
            if (mbuf[lin] == static_cast<int16_t>(label)) {
                ++s.voxelCount;
                float v = ibuf[lin];
                sumI  += v;
                sumI2 += v * v;
                s.bboxX0 = std::min(s.bboxX0, x);
                s.bboxY0 = std::min(s.bboxY0, y);
                s.bboxZ0 = std::min(s.bboxZ0, z);
                s.bboxX1 = std::max(s.bboxX1, x);
                s.bboxY1 = std::max(s.bboxY1, y);
                s.bboxZ1 = std::max(s.bboxZ1, z);
            }
        }

    if (s.voxelCount > 0) {
        s.volumeMm3      = s.voxelCount * sp * sp * sp;
        double mean      = sumI / s.voxelCount;
        s.meanIntensity  = float(mean);
        double var       = sumI2 / s.voxelCount - mean * mean;
        s.stdIntensity   = float(std::sqrt(std::max(0.0, var)));
    }
    return s;
}

std::vector<ROIVolume::LabelStats> ROIVolume::computeAllStats() const
{
    std::vector<LabelStats> result;
    if (!m_mask) return result;
    const int16_t* buf = m_mask->GetBufferPointer();
    int total = nx() * ny() * nz();
    std::set<int16_t> labels;
    for (int i = 0; i < total; ++i)
        if (buf[i] > 0) labels.insert(buf[i]);
    for (int16_t lbl : labels)
        result.push_back(computeStats(lbl));
    return result;
}

std::array<double,3> ROIVolume::labelCentroid(int label) const
{
    if (!m_mask) return {0.0, 0.0, 0.0};
    const int16_t* buf = m_mask->GetBufferPointer();
    int NX = nx(), NY = ny(), NZ = nz();
    double sx = 0, sy = 0, sz = 0;
    int n = 0;
    for (int z = 0; z < NZ; ++z)
      for (int y = 0; y < NY; ++y)
        for (int x = 0; x < NX; ++x) {
            if (buf[linearIdx(x, y, z, NX, NY)] == static_cast<int16_t>(label)) {
                sx += x; sy += y; sz += z; ++n;
            }
        }
    if (n == 0) return {NX/2.0, NY/2.0, NZ/2.0};
    return {sx/n, sy/n, sz/n};
}

// ── Slice propagation ─────────────────────────────────────────────────────────

int ROIVolume::propagateLabel(int label, int axis, int axisIdx, int direction)
{
    if (!m_mask) return -1;
    int NX = nx(), NY = ny(), NZ = nz();
    int targetIdx = axisIdx + direction;
    int maxIdx = (axis == 0 ? NX : axis == 1 ? NY : NZ) - 1;
    if (targetIdx < 0 || targetIdx > maxIdx) return 0;

    int16_t* buf = m_mask->GetBufferPointer();
    std::vector<std::array<int,3>> changed;
    std::vector<int16_t>           oldVals;

    auto trySet = [&](int tx, int ty, int tz) {
        int lin = linearIdx(tx, ty, tz, NX, NY);
        if (buf[lin] != static_cast<int16_t>(label)) {
            changed.push_back({tx, ty, tz});
            oldVals.push_back(buf[lin]);
            buf[lin] = static_cast<int16_t>(label);
        }
    };

    if (axis == 0) {
        for (int z = 0; z < NZ; ++z)
          for (int y = 0; y < NY; ++y)
            if (buf[linearIdx(axisIdx, y, z, NX, NY)] == static_cast<int16_t>(label))
                trySet(targetIdx, y, z);
    } else if (axis == 1) {
        for (int z = 0; z < NZ; ++z)
          for (int x = 0; x < NX; ++x)
            if (buf[linearIdx(x, axisIdx, z, NX, NY)] == static_cast<int16_t>(label))
                trySet(x, targetIdx, z);
    } else {
        for (int y = 0; y < NY; ++y)
          for (int x = 0; x < NX; ++x)
            if (buf[linearIdx(x, y, axisIdx, NX, NY)] == static_cast<int16_t>(label))
                trySet(x, y, targetIdx);
    }

    if (!changed.empty())
        pushUndo(std::move(changed), std::move(oldVals));
    notifyChange();
    return static_cast<int>(changed.size());
}

// ── CSV export ────────────────────────────────────────────────────────────────

bool ROIVolume::exportStatsCSV(const std::string& path) const
{
    auto stats = computeAllStats();
    std::ofstream f(path);
    if (!f) return false;
    f << "Label,VoxelCount,VolumeMm3,MeanIntensity,StdIntensity,"
         "BBoxX0,BBoxY0,BBoxZ0,BBoxX1,BBoxY1,BBoxZ1\n";
    for (auto& s : stats) {
        f << s.label      << ","
          << s.voxelCount << ","
          << s.volumeMm3  << ","
          << s.meanIntensity << ","
          << s.stdIntensity  << ","
          << s.bboxX0 << "," << s.bboxY0 << "," << s.bboxZ0 << ","
          << s.bboxX1 << "," << s.bboxY1 << "," << s.bboxZ1 << "\n";
    }
    return true;
}

// ── Orientation labels ────────────────────────────────────────────────────────

std::array<std::string,4> ROIVolume::sliceOrientLabels(int axis) const
{
    if (!m_displayImg) return {"","","",""};

    // ITK direction matrix: dir[physRow][voxCol]
    // physRow: 0=L(left+), 1=P(post+), 2=S(sup+) in LPS convention
    auto dir = m_displayImg->GetDirection();

    // Which voxel axes span this slice view?
    //   axis 0 (sag, x=const):  rows = vox-Y(1), cols = vox-Z(2)
    //   axis 1 (cor, y=const):  rows = vox-X(0), cols = vox-Z(2)
    //   axis 2 (axi, z=const):  rows = vox-X(0), cols = vox-Y(1)
    int rowVox = (axis == 0) ? 1 : 0;
    int colVox = (axis == 2) ? 1 : 2;

    auto dominant = [&](int voxAxis, bool positive) -> std::string {
        double l = dir[0][voxAxis];
        double p = dir[1][voxAxis];
        double s = dir[2][voxAxis];
        double al = std::abs(l), ap = std::abs(p), as_ = std::abs(s);
        char c;
        if      (al >= ap && al >= as_) c = (l > 0) ? 'L' : 'R';
        else if (ap >= al && ap >= as_) c = (p > 0) ? 'P' : 'A';
        else                            c = (s > 0) ? 'S' : 'I';
        if (!positive) {
            static const char flip[] = "RLPASI";
            static const char orig[] = "LRAPIS";  // unused — manual flip:
            if      (c=='L') c='R'; else if (c=='R') c='L';
            else if (c=='P') c='A'; else if (c=='A') c='P';
            else if (c=='S') c='I'; else if (c=='I') c='S';
        }
        return std::string(1, c);
    };

    // rows go top→bottom = +rowVox direction; top = negative rowVox
    // cols go left→right = +colVox direction; left = negative colVox
    return {
        dominant(rowVox, false),   // top
        dominant(rowVox, true),    // bottom
        dominant(colVox, false),   // left
        dominant(colVox, true)     // right
    };
}

// ── Image registration ────────────────────────────────────────────────────────

bool ROIVolume::loadRegisteredImage(const std::string& movingPath)
{
    if (!m_origImg) return false;
    try {
        // Load moving image
        FloatPtr moving;
        if (fs::is_directory(movingPath))
            moving = loadDicomSeries(movingPath);
        else
            moving = loadNiftiOrMeta(movingPath);
        if (!moving) return false;

        std::cout << "  Registration: aligning moving image to fixed...\n";

        using TxType    = itk::Euler3DTransform<double>;
        using MetricType = itk::MeanSquaresImageToImageMetricv4<FloatImage3, FloatImage3>;
        using OptimizerType = itk::RegularStepGradientDescentOptimizerv4<double>;
        using RegistrationType = itk::ImageRegistrationMethodv4<FloatImage3, FloatImage3, TxType>;
        using InitializerType  = itk::CenteredTransformInitializer<TxType, FloatImage3, FloatImage3>;

        auto metric      = MetricType::New();
        auto optimizer   = OptimizerType::New();
        auto registration = RegistrationType::New();

        auto tx = TxType::New();
        auto initializer = InitializerType::New();
        initializer->SetTransform(tx);
        initializer->SetFixedImage(m_origImg);
        initializer->SetMovingImage(moving);
        initializer->MomentsOn();
        initializer->InitializeTransform();

        optimizer->SetLearningRate(0.1);
        optimizer->SetMinimumStepLength(1e-6);
        optimizer->SetNumberOfIterations(200);
        optimizer->SetRelaxationFactor(0.5);

        registration->SetMetric(metric);
        registration->SetOptimizer(optimizer);
        registration->SetInitialTransform(tx);
        registration->SetFixedImage(m_origImg);
        registration->SetMovingImage(moving);

        RegistrationType::ShrinkFactorsArrayType shrinkFactors(2);
        shrinkFactors[0] = 4; shrinkFactors[1] = 2;
        RegistrationType::SmoothingSigmasArrayType smoothSigmas(2);
        smoothSigmas[0] = 2.0; smoothSigmas[1] = 1.0;
        registration->SetNumberOfLevels(2);
        registration->SetShrinkFactorsPerLevel(shrinkFactors);
        registration->SetSmoothingSigmasPerLevel(smoothSigmas);
        registration->Update();

        // Resample moving → fixed space
        using NNInterp  = itk::NearestNeighborInterpolateImageFunction<FloatImage3, double>;
        using ResampleF = itk::ResampleImageFilter<FloatImage3, FloatImage3>;
        auto resample = ResampleF::New();
        resample->SetInput(moving);
        resample->SetTransform(registration->GetModifiableTransform());
        resample->SetReferenceImage(m_origImg);
        resample->UseReferenceImageOn();
        resample->SetInterpolator(itk::LinearInterpolateImageFunction<FloatImage3,double>::New());
        resample->SetDefaultPixelValue(0.f);
        resample->Update();

        // Replace display image (keep mask)
        m_displayImg = resampleIsotropic(resample->GetOutput(), TARGET_SIZE);
        computeWindow();
        std::cout << "  Registration complete.\n";
        notifyChange();
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "loadRegisteredImage error: " << e.what() << "\n";
        return false;
    }
}

// ── Registration to a reference volume ──────────────────────────────────────────

namespace {

using RegImage = ROIVolume::FloatImage3;
using RegPtr   = ROIVolume::FloatPtr;

// Resample `moving` by `tx` onto `fixed`'s grid (linear interpolation).
template<typename TxPtr>
RegPtr resampleByTransform(RegPtr moving, RegPtr fixed, TxPtr tx)
{
    using ResampleF = itk::ResampleImageFilter<RegImage, RegImage>;
    auto resample = ResampleF::New();
    resample->SetInput(moving);
    resample->SetTransform(tx);
    resample->SetReferenceImage(fixed);
    resample->UseReferenceImageOn();
    resample->SetInterpolator(
        itk::LinearInterpolateImageFunction<RegImage, double>::New());
    resample->SetDefaultPixelValue(0.f);
    resample->Update();
    return resample->GetOutput();
}

// Multi-resolution linear (rigid/affine) registration; returns optimized transform.
template<typename TTx>
typename TTx::Pointer registerLinear(RegPtr fixed, RegPtr moving, int iters)
{
    using MetricT = itk::MattesMutualInformationImageToImageMetricv4<RegImage, RegImage>;
    using OptT    = itk::RegularStepGradientDescentOptimizerv4<double>;
    using RegT    = itk::ImageRegistrationMethodv4<RegImage, RegImage, TTx>;
    using InitT   = itk::CenteredTransformInitializer<TTx, RegImage, RegImage>;
    using ScalesT = itk::RegistrationParameterScalesFromPhysicalShift<MetricT>;

    auto metric = MetricT::New();
    metric->SetNumberOfHistogramBins(50);

    auto opt = OptT::New();
    opt->SetLearningRate(1.0);
    opt->SetMinimumStepLength(1e-4);
    opt->SetNumberOfIterations(iters);
    opt->SetRelaxationFactor(0.6);

    auto tx   = TTx::New();
    auto init = InitT::New();
    init->SetTransform(tx);
    init->SetFixedImage(fixed);
    init->SetMovingImage(moving);
    init->MomentsOn();
    init->InitializeTransform();

    auto reg = RegT::New();
    reg->SetMetric(metric);
    reg->SetOptimizer(opt);
    reg->SetInitialTransform(tx);
    reg->SetFixedImage(fixed);
    reg->SetMovingImage(moving);

    auto scales = ScalesT::New();
    scales->SetMetric(metric);
    opt->SetScalesEstimator(scales);

    typename RegT::ShrinkFactorsArrayType   shrink(3);
    shrink[0] = 4; shrink[1] = 2; shrink[2] = 1;
    typename RegT::SmoothingSigmasArrayType sigma(3);
    sigma[0] = 2.0; sigma[1] = 1.0; sigma[2] = 0.0;
    reg->SetNumberOfLevels(3);
    reg->SetShrinkFactorsPerLevel(shrink);
    reg->SetSmoothingSigmasPerLevel(sigma);
    reg->Update();

    return tx;   // v4 optimizes the transform passed to SetInitialTransform
}

} // namespace

// Pure registration — no shared state mutated, safe to run off-thread.
ROIVolume::FloatPtr ROIVolume::registerImages(FloatPtr movingImg, FloatPtr fixedImg,
                                              int mode, int iterations,
                                              TransformPtr* outTx)
{
    if (!movingImg || !fixedImg) return nullptr;
    try {
        if (mode == 1) {                       // Affine
            auto tx = registerLinear<itk::AffineTransform<double,3>>(
                          fixedImg, movingImg, iterations);
            if (outTx) *outTx = tx.GetPointer();
            return resampleByTransform(movingImg, fixedImg, tx);
        }
        else if (mode == 2) {                  // Deformable (rigid pre-align + BSpline)
            auto rigid = registerLinear<itk::Euler3DTransform<double>>(
                             fixedImg, movingImg, iterations);
            RegPtr preAligned = resampleByTransform(movingImg, fixedImg, rigid);

            using BTx   = itk::BSplineTransform<double,3,3>;
            using BInit = itk::BSplineTransformInitializer<BTx, RegImage>;
            using MetricT = itk::MattesMutualInformationImageToImageMetricv4<RegImage, RegImage>;
            using OptT    = itk::RegularStepGradientDescentOptimizerv4<double>;
            using RegT    = itk::ImageRegistrationMethodv4<RegImage, RegImage, BTx>;
            using ScalesT = itk::RegistrationParameterScalesFromPhysicalShift<MetricT>;

            auto btx   = BTx::New();
            auto binit = BInit::New();
            binit->SetTransform(btx);
            binit->SetImage(fixedImg);
            BTx::MeshSizeType mesh; mesh.Fill(8);
            binit->SetTransformDomainMeshSize(mesh);
            binit->InitializeTransform();

            auto metric = MetricT::New(); metric->SetNumberOfHistogramBins(50);
            auto opt = OptT::New();
            opt->SetLearningRate(1.0);
            opt->SetMinimumStepLength(1e-4);
            opt->SetNumberOfIterations(std::max(20, iterations / 2));
            opt->SetRelaxationFactor(0.6);

            auto reg = RegT::New();
            reg->SetMetric(metric);
            reg->SetOptimizer(opt);
            reg->SetInitialTransform(btx);
            reg->SetFixedImage(fixedImg);
            reg->SetMovingImage(preAligned);

            auto scales = ScalesT::New();
            scales->SetMetric(metric);
            opt->SetScalesEstimator(scales);

            typename RegT::ShrinkFactorsArrayType   shrink(3);
            shrink[0] = 4; shrink[1] = 2; shrink[2] = 1;
            typename RegT::SmoothingSigmasArrayType sigma(3);
            sigma[0] = 2.0; sigma[1] = 1.0; sigma[2] = 0.0;
            reg->SetNumberOfLevels(3);
            reg->SetShrinkFactorsPerLevel(shrink);
            reg->SetSmoothingSigmasPerLevel(sigma);
            reg->Update();

            if (outTx) {
                auto comp = itk::CompositeTransform<double,3>::New();
                comp->AddTransform(rigid);   // applied after btx
                comp->AddTransform(btx);     // applied first
                *outTx = comp.GetPointer();
            }
            return resampleByTransform(preAligned, fixedImg, btx);
        }
        // Rigid (Euler3D)
        auto tx = registerLinear<itk::Euler3DTransform<double>>(
                      fixedImg, movingImg, iterations);
        if (outTx) *outTx = tx.GetPointer();
        return resampleByTransform(movingImg, fixedImg, tx);
    }
    catch (const std::exception& e) {
        std::cerr << "registerImages error: " << e.what() << "\n";
        return nullptr;
    }
}

ROIVolume::FloatPtr ROIVolume::ensureBackupAndMovingSource()
{
    if (m_displayImg && !m_displayBackup) m_displayBackup = m_displayImg;
    return m_displayBackup;
}

void ROIVolume::applyRegisteredImage(FloatPtr img)
{
    if (!img) return;
    m_displayImg = img;
    m_mask = createMask(m_displayImg);   // moving masks are unused in fusion
    m_history.clear();
    notifyChange();
}

bool ROIVolume::registerTo(const ROIVolume* fixed, int mode, int iterations)
{
    if (!m_displayImg || !fixed || !fixed->m_displayImg) return false;
    FloatPtr moving = ensureBackupAndMovingSource();
    TransformPtr tx;
    FloatPtr result = registerImages(moving, fixed->m_displayImg, mode, iterations, &tx);
    if (!result) return false;
    applyRegisteredImage(result);
    m_lastTransform = tx;
    return true;
}

bool ROIVolume::saveTransform(const std::string& path) const
{
    if (!m_lastTransform) return false;
    try {
        auto w = itk::TransformFileWriterTemplate<double>::New();
        w->SetInput(m_lastTransform);
        w->SetFileName(path);
        w->Update();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "saveTransform error: " << e.what() << "\n";
        return false;
    }
}

bool ROIVolume::loadTransform(const std::string& path, const ROIVolume* fixed)
{
    if (!m_displayImg || !fixed || !fixed->m_displayImg) return false;
    try {
        auto reader = itk::TransformFileReaderTemplate<double>::New();
        reader->SetFileName(path);
        reader->Update();
        auto list = reader->GetTransformList();
        if (!list || list->empty()) return false;
        TransformPtr tx = dynamic_cast<itk::Transform<double,3,3>*>(list->front().GetPointer());
        if (!tx) return false;

        if (!m_displayBackup) m_displayBackup = m_displayImg;
        using ResampleF = itk::ResampleImageFilter<FloatImage3, FloatImage3>;
        auto resample = ResampleF::New();
        resample->SetInput(m_displayBackup);
        resample->SetTransform(tx);
        resample->SetReferenceImage(fixed->m_displayImg);
        resample->UseReferenceImageOn();
        resample->SetInterpolator(
            itk::LinearInterpolateImageFunction<FloatImage3, double>::New());
        resample->SetDefaultPixelValue(0.f);
        resample->Update();

        applyRegisteredImage(resample->GetOutput());
        m_lastTransform = tx;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "loadTransform error: " << e.what() << "\n";
        return false;
    }
}

bool ROIVolume::applyManualTransform(const ROIVolume* fixed,
                                      double tx, double ty, double tz,
                                      double rxDeg, double ryDeg, double rzDeg)
{
    if (!m_displayImg || !fixed || !fixed->m_displayImg) return false;
    try {
        if (!m_displayBackup) m_displayBackup = m_displayImg;
        RegPtr movingImg = m_displayBackup;
        RegPtr fixedImg  = fixed->m_displayImg;

        using Euler = itk::Euler3DTransform<double>;
        auto e = Euler::New();

        auto size = movingImg->GetLargestPossibleRegion().GetSize();
        itk::ContinuousIndex<double,3> cidx;
        cidx[0] = (size[0]-1)/2.0; cidx[1] = (size[1]-1)/2.0; cidx[2] = (size[2]-1)/2.0;
        Euler::InputPointType center;
        movingImg->TransformContinuousIndexToPhysicalPoint(cidx, center);
        e->SetCenter(center);

        const double d2r = std::acos(-1.0) / 180.0;
        e->SetRotation(rxDeg*d2r, ryDeg*d2r, rzDeg*d2r);
        Euler::OutputVectorType t; t[0]=tx; t[1]=ty; t[2]=tz;
        e->SetTranslation(t);

        m_displayImg = resampleByTransform(movingImg, fixedImg, e);
        m_lastTransform = e.GetPointer();
        m_mask = createMask(m_displayImg);
        m_history.clear();
        notifyChange();
        return true;
    }
    catch (const std::exception& ex) {
        std::cerr << "applyManualTransform error: " << ex.what() << "\n";
        return false;
    }
}

bool ROIVolume::resetRegistration()
{
    if (!m_displayBackup) return false;
    m_displayImg    = m_displayBackup;
    m_displayBackup = nullptr;
    m_mask = createMask(m_displayImg);
    m_history.clear();
    notifyChange();
    return true;
}

bool ROIVolume::flipAxis(int axis)
{
    // Mirror the moving image about its centre along a physical axis:
    // axis 0 = X (L/R) · 1 = Y (A/P) · 2 = Z (H/F), assuming LPS orientation.
    // Each call toggles that flip; resetRegistration() restores the original.
    if (!m_displayImg || axis < 0 || axis > 2) return false;
    try {
        if (!m_displayBackup) m_displayBackup = m_displayImg;
        RegPtr cur = m_displayImg;          // flip current image in its own frame

        using Affine = itk::AffineTransform<double, 3>;
        auto a = Affine::New();

        auto size = cur->GetLargestPossibleRegion().GetSize();
        itk::ContinuousIndex<double,3> cidx;
        cidx[0] = (size[0]-1)/2.0; cidx[1] = (size[1]-1)/2.0; cidx[2] = (size[2]-1)/2.0;
        Affine::InputPointType center;
        cur->TransformContinuousIndexToPhysicalPoint(cidx, center);
        a->SetCenter(center);

        Affine::MatrixType m; m.SetIdentity();
        m[axis][axis] = -1.0;               // reflection along the chosen axis
        a->SetMatrix(m);

        m_displayImg = resampleByTransform(cur, cur, a);
        m_mask = createMask(m_displayImg);
        m_history.clear();
        notifyChange();
        return true;
    }
    catch (const std::exception& ex) {
        std::cerr << "flipAxis error: " << ex.what() << "\n";
        return false;
    }
}
