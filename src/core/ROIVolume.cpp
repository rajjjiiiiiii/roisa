// ROIVolume.cpp — Core data model implementation

#include "ROIVolume.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
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

bool ROIVolume::load(const std::string& path)
{
    try {
        FloatPtr raw;
        if (fs::is_directory(path))
            raw = loadDicomSeries(path);
        else
            raw = loadNiftiOrMeta(path);

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

ROIVolume::FloatPtr ROIVolume::loadDicomSeries(const std::string& dir)
{
    // Detect GDCM series IDs
    auto nameGen = itk::GDCMSeriesFileNames::New();
    nameGen->SetUseSeriesDetails(true);
    nameGen->SetDirectory(dir);

    const auto& seriesIds = nameGen->GetSeriesUIDs();
    if (seriesIds.empty())
        throw std::runtime_error("No DICOM series found in: " + dir);

    auto fileNames = nameGen->GetFileNames(seriesIds.front());
    std::cout << "  DICOM: series " << seriesIds.front()
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

void ROIVolume::getIntensitySlice(int axis, int idx,
                                   std::vector<float>& dst) const
{
    if (!m_displayImg) return;
    const float* buf = m_displayImg->GetBufferPointer();
    int NX = nx(), NY = ny(), NZ = nz();
    int rows = sliceRows(axis), cols = sliceCols(axis);
    dst.resize(rows * cols);

    // axis 0 (sagittal x=idx): pixel[row=iy, col=iz] = buf[idx + NX*iy + NX*NY*iz]
    // axis 1 (coronal  y=idx): pixel[row=ix, col=iz] = buf[ix  + NX*idx + NX*NY*iz]
    // axis 2 (axial    z=idx): pixel[row=ix, col=iy] = buf[ix  + NX*iy  + NX*NY*idx]
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

// ── Mask bulk write ───────────────────────────────────────────────────────────

void ROIVolume::replaceMask(Int16Ptr newMask)
{
    m_mask = newMask;
    notifyChange();
}

// ── Clear ─────────────────────────────────────────────────────────────────────

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
    if (m_onChange) m_onChange();
}
