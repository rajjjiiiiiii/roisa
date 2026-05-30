// ROIAlgorithms.cpp — Segmentation and morphological algorithm implementations

#include "ROIAlgorithms.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>

// ITK filters
#include <itkBinaryBallStructuringElement.h>
#include <itkBinaryDilateImageFilter.h>
#include <itkBinaryErodeImageFilter.h>
#include <itkBinaryFillholeImageFilter.h>
#include <itkCastImageFilter.h>
#include <itkConfidenceConnectedImageFilter.h>
#include <itkConnectedComponentImageFilter.h>
#include <itkConnectedThresholdImageFilter.h>
#include <itkExtractImageFilter.h>
#include <itkFastMarchingImageFilter.h>
#include <itkGeodesicActiveContourLevelSetImageFilter.h>
#include <itkGradientMagnitudeImageFilter.h>
#include <itkIdentityTransform.h>
#include <itkImageRegionIterator.h>
#include <itkMorphologicalWatershedImageFilter.h>
#include <itkNeighborhoodConnectedImageFilter.h>
#include <itkNearestNeighborInterpolateImageFunction.h>
#include <itkOtsuMultipleThresholdsImageFilter.h>
#include <itkOtsuThresholdImageFilter.h>
#include <itkRelabelComponentImageFilter.h>
#include <itkRescaleIntensityImageFilter.h>
#include <itkScalarImageKmeansImageFilter.h>
#include <itkSignedMaurerDistanceMapImageFilter.h>
#include <itkSmoothingRecursiveGaussianImageFilter.h>
#include <itkBoundedReciprocalImageFilter.h>
#include <itkMaskImageFilter.h>

// ── Type aliases ─────────────────────────────────────────────────────────────
using FloatImg = ROIVolume::FloatImage3;
using Int16Img = ROIVolume::Int16Image3;
using Uint8Img = ROIVolume::Uint8Image3;
using FloatPtr = ROIVolume::FloatPtr;
using Int16Ptr = ROIVolume::Int16Ptr;
using Uint8Ptr = ROIVolume::Uint8Ptr;

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

// Convert display-space (x,y,z) to ITK Index
inline FloatImg::IndexType toIdx(int x, int y, int z)
{
    FloatImg::IndexType idx;
    idx[0] = x; idx[1] = y; idx[2] = z;
    return idx;
}

static inline int linIdx(int x, int y, int z, int nx, int ny)
{
    return x + nx * y + nx * ny * z;
}

// Collect {indices, oldValues} for all voxels currently assigned `label`
// (or all non-zero if label < 0). Used to build undo entries.
std::pair<std::vector<std::array<int,3>>, std::vector<int16_t>>
collectUndo(ROIVolume& vol, int label)
{
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t>           olds;
    const int16_t* buf = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    for (int z = 0; z < NZ; ++z)
      for (int y = 0; y < NY; ++y)
        for (int x = 0; x < NX; ++x) {
            int16_t v = buf[linIdx(x,y,z,NX,NY)];
            if (label < 0 ? v != 0 : v == static_cast<int16_t>(label)) {
                idxs.push_back({x,y,z});
                olds.push_back(v);
            }
        }
    return {idxs, olds};
}

// Convert bool array (nx*ny*nz, x-fastest) back to an Int16 ITK image
// with the same geometry as ref, assigning `label` to true voxels.
Int16Ptr boolToMaskLabel(const std::vector<bool>& arr,
                          FloatPtr ref, int16_t label)
{
    auto out = Int16Img::New();
    out->SetRegions(ref->GetLargestPossibleRegion());
    out->SetSpacing(ref->GetSpacing());
    out->SetOrigin(ref->GetOrigin());
    out->SetDirection(ref->GetDirection());
    out->Allocate(true);
    int16_t* buf = out->GetBufferPointer();
    for (size_t i = 0; i < arr.size(); ++i)
        buf[i] = arr[i] ? label : 0;
    return out;
}

// Extract a uint8 binary image for a given label from the mask
Uint8Ptr labelToBinary(Int16Ptr mask, int16_t label)
{
    auto out = Uint8Img::New();
    out->SetRegions(mask->GetLargestPossibleRegion());
    out->SetSpacing(mask->GetSpacing());
    out->SetOrigin(mask->GetOrigin());
    out->SetDirection(mask->GetDirection());
    out->Allocate(true);
    const int16_t* src = mask->GetBufferPointer();
    uint8_t*       dst = out->GetBufferPointer();
    size_t n = mask->GetLargestPossibleRegion().GetNumberOfPixels();
    for (size_t i = 0; i < n; ++i)
        dst[i] = (src[i] == label) ? 1u : 0u;
    return out;
}

// Write uint8 binary image back into mask at label positions
void binaryToMask(Int16Ptr mask, Uint8Ptr bin, int16_t label,
                  bool clearFirst = true)
{
    int16_t* mBuf = mask->GetBufferPointer();
    const uint8_t* bBuf = bin->GetBufferPointer();
    size_t n = mask->GetLargestPossibleRegion().GetNumberOfPixels();
    if (clearFirst) {
        // Clear existing occurrences of label
        for (size_t i = 0; i < n; ++i)
            if (mBuf[i] == label) mBuf[i] = 0;
    }
    for (size_t i = 0; i < n; ++i)
        if (bBuf[i]) mBuf[i] = label;
}

// Build brush footprint voxel list: sphere / cylinder / cube
void brushFootprint(int cx, int cy, int cz,
                    int radius, int shape, int viewAxis, bool twoD,
                    int NX, int NY, int NZ,
                    std::vector<std::array<int,3>>& out)
{
    int r = std::max(1, radius);
    int xr = twoD && viewAxis==0 ? 0 : r;
    int yr = twoD && viewAxis==1 ? 0 : r;
    int zr = twoD && viewAxis==2 ? 0 : r;

    for (int dz = -zr; dz <= zr; ++dz)
      for (int dy = -yr; dy <= yr; ++dy)
        for (int dx = -xr; dx <= xr; ++dx) {
            int x = cx+dx, y = cy+dy, z = cz+dz;
            if (x<0||x>=NX||y<0||y>=NY||z<0||z>=NZ) continue;
            bool hit = false;
            if (shape == 2) {          // cube
                hit = true;
            } else if (shape == 1) {   // cylinder
                if (viewAxis==2) hit = dx*dx + dy*dy <= r*r;
                else if (viewAxis==1) hit = dx*dx + dz*dz <= r*r;
                else                  hit = dy*dy + dz*dz <= r*r;
            } else {                   // sphere
                hit = dx*dx + dy*dy + dz*dz <= r*r;
            }
            if (hit) out.push_back({x, y, z});
        }
}

} // anonymous namespace

// ════════════════════════════════════════════════════════════════════════════
// Brush
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::paintBrush(ROIVolume& vol,
                               int cx, int cy, int cz,
                               int radius, int16_t label,
                               int brushShape, int viewAxis, bool twoD)
{
    std::vector<std::array<int,3>> voxels;
    brushFootprint(cx, cy, cz, radius, brushShape, viewAxis, twoD,
                   vol.nx(), vol.ny(), vol.nz(), voxels);

    int16_t* buf = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny();

    // Capture undo for touched voxels
    std::vector<int16_t> olds;
    olds.reserve(voxels.size());
    for (auto& [x,y,z] : voxels)
        olds.push_back(buf[linIdx(x,y,z,NX,NY)]);

    vol.pushUndo(voxels, olds);
    for (auto& [x,y,z] : voxels)
        buf[linIdx(x,y,z,NX,NY)] = label;

    vol.notifyChange();
    return static_cast<int>(voxels.size());
}

// ════════════════════════════════════════════════════════════════════════════
// Threshold
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::thresholdSegment(ROIVolume& vol,
                                    float lower, float upper, int16_t label,
                                    int axis, int sliceIdx)
{
    const float*   imgBuf  = vol.displayImage()->GetBufferPointer();
    int16_t*       mskBuf  = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();

    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t>           olds;

    auto test = [&](int x, int y, int z) {
        float v = imgBuf[linIdx(x,y,z,NX,NY)];
        if (v >= lower && v <= upper) {
            int lin = linIdx(x,y,z,NX,NY);
            idxs.push_back({x,y,z});
            olds.push_back(mskBuf[lin]);
        }
    };

    if (axis >= 0 && sliceIdx >= 0) {
        // Restrict to one slice
        for (int a = 0; a < (axis==0?NY:NX); ++a)
          for (int b = 0; b < (axis==2?NY:(axis==1?NZ:NZ)); ++b) {
              int x, y, z;
              if (axis==0) { x=sliceIdx; y=a; z=b; }
              else if (axis==1) { x=a; y=sliceIdx; z=b; }
              else              { x=a; y=b; z=sliceIdx; }
              test(x, y, z);
          }
    } else {
        for (int z = 0; z < NZ; ++z)
          for (int y = 0; y < NY; ++y)
            for (int x = 0; x < NX; ++x)
                test(x, y, z);
    }

    vol.pushUndo(idxs, olds);
    for (auto& [x,y,z] : idxs)
        mskBuf[linIdx(x,y,z,NX,NY)] = label;

    int n = static_cast<int>(idxs.size());
    std::cout << "  Threshold [" << lower << ", " << upper << "]  ->  label "
              << label << "  (" << n << " voxels)\n";
    vol.notifyChange();
    return n;
}

// ════════════════════════════════════════════════════════════════════════════
// Otsu
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::otsuThreshold(ROIVolume& vol, int16_t label,
                                  int nBins, int nClasses)
{
    vol.pushUndoAll();
    if (nClasses <= 1) {
        using OtsuFilter = itk::OtsuThresholdImageFilter<FloatImg, Int16Img>;
        auto f = OtsuFilter::New();
        f->SetInput(vol.displayImage());
        f->SetInsideValue(0);
        f->SetOutsideValue(label);
        f->SetNumberOfHistogramBins(nBins);
        f->Update();
        vol.replaceMask(f->GetOutput());
        auto n = static_cast<int>(f->GetOutput()->GetLargestPossibleRegion().GetNumberOfPixels());
        std::cout << "  Otsu -> label " << label << "\n";
        return n;
    } else {
        using OtsuMulti = itk::OtsuMultipleThresholdsImageFilter<FloatImg, Int16Img>;
        auto f = OtsuMulti::New();
        f->SetInput(vol.displayImage());
        f->SetNumberOfThresholds(nClasses);
        f->SetNumberOfHistogramBins(nBins);
        f->SetLabelOffset(0);
        f->Update();

        // Map classifier output (0..nClasses) → user labels (label..label+nClasses)
        Int16Ptr result = f->GetOutput();
        Int16Ptr newMask = vol.maskImage();
        const int16_t* src = result->GetBufferPointer();
        int16_t*       dst = vol.maskImage()->GetBufferPointer();
        size_t np = vol.maskImage()->GetLargestPossibleRegion().GetNumberOfPixels();
        for (size_t i = 0; i < np; ++i)
            dst[i] = (src[i] > 0)
                     ? static_cast<int16_t>(label + src[i] - 1)
                     : 0;
        std::cout << "  OtsuMulti(" << nClasses << " classes) -> labels "
                  << label << "-" << (label + nClasses - 1) << "\n";
        vol.notifyChange();
        return static_cast<int>(np);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// KMeans
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::kMeansCluster(ROIVolume& vol, int k, int16_t labelOffset)
{
    vol.pushUndoAll();
    // Seed with evenly spaced percentiles
    const float* buf = vol.displayImage()->GetBufferPointer();
    size_t np = vol.displayImage()->GetLargestPossibleRegion().GetNumberOfPixels();
    std::vector<float> sorted(buf, buf + np);
    std::sort(sorted.begin(), sorted.end());

    using KMFilter = itk::ScalarImageKmeansImageFilter<FloatImg>;
    auto f = KMFilter::New();
    f->SetInput(vol.displayImage());
    for (int i = 0; i < k; ++i) {
        double pct = 5.0 + 90.0 * i / (k - 1 + 1e-9);
        size_t pi  = static_cast<size_t>(pct / 100.0 * (sorted.size() - 1));
        f->AddClassWithInitialMean(static_cast<double>(sorted[pi]));
    }
    f->Update();

    // Map 0..k-1 → labelOffset..labelOffset+k-1
    const auto* src = f->GetOutput()->GetBufferPointer();
    int16_t* dst = vol.maskImage()->GetBufferPointer();
    for (size_t i = 0; i < np; ++i)
        dst[i] = static_cast<int16_t>(labelOffset + src[i]);

    std::cout << "  KMeans k=" << k << " -> labels " << labelOffset
              << "-" << (labelOffset + k - 1) << "\n";
    vol.notifyChange();
    return static_cast<int>(np);
}

// ════════════════════════════════════════════════════════════════════════════
// Region grow (BFS)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::regionGrow(ROIVolume& vol,
                               int sx, int sy, int sz,
                               float tolerance, int16_t label,
                               int maxVoxels)
{
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    if (sx<0||sx>=NX||sy<0||sy>=NY||sz<0||sz>=NZ) return -1;

    float seedVal = vol.getIntensity(sx, sy, sz);
    float lo = seedVal - tolerance, hi = seedVal + tolerance;

    std::vector<bool> visited(NX * NY * NZ, false);
    std::queue<std::array<int,3>> q;
    q.push({sx, sy, sz});

    std::vector<std::array<int,3>> grown;

    while (!q.empty() && static_cast<int>(grown.size()) < maxVoxels) {
        auto [x, y, z] = q.front(); q.pop();
        int lin = linIdx(x, y, z, NX, NY);
        if (visited[lin]) continue;
        visited[lin] = true;
        float v = vol.getIntensity(x, y, z);
        if (v < lo || v > hi) continue;
        grown.push_back({x, y, z});
        static const int dx[]={1,-1,0,0,0,0};
        static const int dy[]={0, 0,1,-1,0,0};
        static const int dz[]={0, 0,0, 0,1,-1};
        for (int d = 0; d < 6; ++d) {
            int nx2=x+dx[d], ny2=y+dy[d], nz2=z+dz[d];
            if (nx2>=0&&nx2<NX&&ny2>=0&&ny2<NY&&nz2>=0&&nz2<NZ)
                if (!visited[linIdx(nx2,ny2,nz2,NX,NY)])
                    q.push({nx2, ny2, nz2});
        }
    }

    if (grown.empty()) {
        std::cout << "  RegionGrow: no voxels found from seed (" << sx << ","
                  << sy << "," << sz << ")\n";
        return 0;
    }

    int16_t* mBuf = vol.maskImage()->GetBufferPointer();
    std::vector<int16_t> olds;
    olds.reserve(grown.size());
    for (auto& g : grown) olds.push_back(mBuf[linIdx(g[0],g[1],g[2],NX,NY)]);

    vol.pushUndo(grown, olds);
    for (auto& g : grown) mBuf[linIdx(g[0],g[1],g[2],NX,NY)] = label;

    std::cout << "  RegionGrow seed=(" << sx << "," << sy << "," << sz
              << ")  val=" << seedVal << "±" << tolerance
              << "  -> label " << label << "  (" << grown.size() << " voxels)\n";
    vol.notifyChange();
    return static_cast<int>(grown.size());
}

// ════════════════════════════════════════════════════════════════════════════
// Connected threshold (ITK)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::connectedThreshold(ROIVolume& vol,
                                       int sx, int sy, int sz,
                                       float lower, float upper, int16_t label)
{
    using CTFilter = itk::ConnectedThresholdImageFilter<FloatImg, Int16Img>;
    auto f = CTFilter::New();
    f->SetInput(vol.displayImage());
    f->SetSeed(toIdx(sx, sy, sz));
    f->SetLower(lower);
    f->SetUpper(upper);
    f->SetReplaceValue(label);
    f->Update();

    Int16Ptr result = f->GetOutput();
    const int16_t* src = result->GetBufferPointer();
    int16_t* dst = vol.maskImage()->GetBufferPointer();
    size_t np = vol.maskImage()->GetLargestPossibleRegion().GetNumberOfPixels();

    // Undo: capture voxels that will change
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t>           olds;
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    for (int z = 0; z < NZ; ++z)
      for (int y = 0; y < NY; ++y)
        for (int x = 0; x < NX; ++x)
            if (src[linIdx(x,y,z,NX,NY)] != 0)
            { idxs.push_back({x,y,z}); olds.push_back(dst[linIdx(x,y,z,NX,NY)]); }

    vol.pushUndo(idxs, olds);
    for (auto& [x,y,z] : idxs) dst[linIdx(x,y,z,NX,NY)] = label;

    std::cout << "  ConnectedThreshold [" << lower << "," << upper
              << "] -> label " << label << "  (" << idxs.size() << " voxels)\n";
    vol.notifyChange();
    return static_cast<int>(idxs.size());
}

// ════════════════════════════════════════════════════════════════════════════
// Neighborhood connected (ITK)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::neighborhoodConnected(ROIVolume& vol,
                                          int sx, int sy, int sz,
                                          float lower, float upper,
                                          int radius, int16_t label)
{
    using NCFilter = itk::NeighborhoodConnectedImageFilter<FloatImg, Int16Img>;
    FloatImg::SizeType rad; rad.Fill(radius);
    auto f = NCFilter::New();
    f->SetInput(vol.displayImage());
    f->SetSeed(toIdx(sx, sy, sz));
    f->SetLower(lower);
    f->SetUpper(upper);
    f->SetRadius(rad);
    f->SetReplaceValue(label);
    f->Update();

    const int16_t* src = f->GetOutput()->GetBufferPointer();
    int16_t* dst = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t> olds;
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x)
        if (src[linIdx(x,y,z,NX,NY)])
        { idxs.push_back({x,y,z}); olds.push_back(dst[linIdx(x,y,z,NX,NY)]); }

    vol.pushUndo(idxs, olds);
    for (auto& [x,y,z] : idxs) dst[linIdx(x,y,z,NX,NY)] = label;
    std::cout << "  NeighborhoodConnected r=" << radius << " -> label " << label
              << "  (" << idxs.size() << " voxels)\n";
    vol.notifyChange();
    return static_cast<int>(idxs.size());
}

// ════════════════════════════════════════════════════════════════════════════
// Confidence connected (ITK)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::confidenceConnected(ROIVolume& vol,
                                        int sx, int sy, int sz,
                                        float multiplier, int iterations,
                                        int radius, int16_t label)
{
    using CCFilter = itk::ConfidenceConnectedImageFilter<FloatImg, Int16Img>;
    auto f = CCFilter::New();
    f->SetInput(vol.displayImage());
    f->SetSeed(toIdx(sx, sy, sz));
    f->SetMultiplier(multiplier);
    f->SetNumberOfIterations(iterations);
    f->SetInitialNeighborhoodRadius(radius);
    f->SetReplaceValue(label);
    f->Update();

    const int16_t* src = f->GetOutput()->GetBufferPointer();
    int16_t* dst = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t> olds;
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x)
        if (src[linIdx(x,y,z,NX,NY)])
        { idxs.push_back({x,y,z}); olds.push_back(dst[linIdx(x,y,z,NX,NY)]); }

    vol.pushUndo(idxs, olds);
    for (auto& [x,y,z] : idxs) dst[linIdx(x,y,z,NX,NY)] = label;
    std::cout << "  ConfidenceConnected mult=" << multiplier
              << " iter=" << iterations << " -> label " << label
              << "  (" << idxs.size() << " voxels)\n";
    vol.notifyChange();
    return static_cast<int>(idxs.size());
}

// ════════════════════════════════════════════════════════════════════════════
// 2D flood fill
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::floodFill2D(ROIVolume& vol,
                                int sx, int sy, int sz,
                                int viewAxis, float tolerance, int16_t label)
{
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    // Determine 2D slice coords
    int fixedIdx, r0, c0, rows, cols;
    if (viewAxis == 0) { fixedIdx=sx; r0=sy; c0=sz; rows=NY; cols=NZ; }
    else if (viewAxis == 1) { fixedIdx=sy; r0=sx; c0=sz; rows=NX; cols=NZ; }
    else                   { fixedIdx=sz; r0=sx; c0=sy; rows=NX; cols=NY; }

    float seedVal;
    if (viewAxis==0) seedVal = vol.getIntensity(fixedIdx, r0, c0);
    else if (viewAxis==1) seedVal = vol.getIntensity(r0, fixedIdx, c0);
    else              seedVal = vol.getIntensity(r0, c0, fixedIdx);

    float lo = seedVal - tolerance, hi = seedVal + tolerance;

    auto getInt = [&](int r, int c) {
        if (viewAxis==0) return vol.getIntensity(fixedIdx, r, c);
        if (viewAxis==1) return vol.getIntensity(r, fixedIdx, c);
        return vol.getIntensity(r, c, fixedIdx);
    };

    std::vector<bool> visited(rows * cols, false);
    std::queue<std::pair<int,int>> q;
    q.push({r0, c0});
    std::vector<std::array<int,2>> grown;

    while (!q.empty()) {
        auto [r, c] = q.front(); q.pop();
        if (r<0||r>=rows||c<0||c>=cols) continue;
        int lin2 = r * cols + c;
        if (visited[lin2]) continue;
        visited[lin2] = true;
        float v = getInt(r, c);
        if (v < lo || v > hi) continue;
        grown.push_back({r, c});
        q.push({r+1,c}); q.push({r-1,c}); q.push({r,c+1}); q.push({r,c-1});
    }

    if (grown.empty()) { std::cout << "  FloodFill2D: no voxels.\n"; return 0; }

    int16_t* mBuf = vol.maskImage()->GetBufferPointer();
    std::vector<std::array<int,3>> idxs3;
    std::vector<int16_t> olds;
    for (auto& [r,c] : grown) {
        std::array<int,3> xyz;
        if (viewAxis==0) xyz={fixedIdx,r,c};
        else if(viewAxis==1) xyz={r,fixedIdx,c};
        else xyz={r,c,fixedIdx};
        idxs3.push_back(xyz);
        olds.push_back(mBuf[linIdx(xyz[0],xyz[1],xyz[2],NX,NY)]);
    }
    vol.pushUndo(idxs3, olds);
    for (auto& [x,y,z] : idxs3) mBuf[linIdx(x,y,z,NX,NY)] = label;

    std::cout << "  FloodFill2D axis=" << viewAxis << "[" << fixedIdx
              << "] -> label " << label << "  (" << grown.size() << " voxels)\n";
    vol.notifyChange();
    return static_cast<int>(grown.size());
}

// ════════════════════════════════════════════════════════════════════════════
// Fast marching (ITK)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::fastMarching(ROIVolume& vol,
                                 int sx, int sy, int sz,
                                 float stoppingValue, int16_t label)
{
    // Normalise intensity to speed image [0.01, 1.0]
    using RescaleF = itk::RescaleIntensityImageFilter<FloatImg, FloatImg>;
    auto rsc = RescaleF::New();
    rsc->SetInput(vol.displayImage());
    rsc->SetOutputMinimum(0.01f);
    rsc->SetOutputMaximum(1.0f);
    rsc->Update();

    using FMFilter = itk::FastMarchingImageFilter<FloatImg, FloatImg>;
    using NodeContainer = FMFilter::NodeContainer;
    using NodeType      = FMFilter::NodeType;

    auto seeds = NodeContainer::New();
    seeds->Initialize();
    NodeType node;
    FloatImg::IndexType seedIdx; seedIdx[0]=sx; seedIdx[1]=sy; seedIdx[2]=sz;
    node.SetValue(0.0f);
    node.SetIndex(seedIdx);
    seeds->InsertElement(0, node);

    auto fm = FMFilter::New();
    fm->SetInput(rsc->GetOutput());
    fm->SetTrialPoints(seeds);
    fm->SetStoppingValue(stoppingValue);
    fm->Update();

    // Voxels with arrival time < stoppingValue are included
    const float* src = fm->GetOutput()->GetBufferPointer();
    int16_t* dst = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t> olds;
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x) {
        float t = src[linIdx(x,y,z,NX,NY)];
        if (t < stoppingValue)
        { idxs.push_back({x,y,z}); olds.push_back(dst[linIdx(x,y,z,NX,NY)]); }
    }
    vol.pushUndo(idxs, olds);
    for (auto& [x,y,z] : idxs) dst[linIdx(x,y,z,NX,NY)] = label;
    std::cout << "  FastMarching stop=" << stoppingValue << " -> label " << label
              << "  (" << idxs.size() << " voxels)\n";
    vol.notifyChange();
    return static_cast<int>(idxs.size());
}

// ════════════════════════════════════════════════════════════════════════════
// Erode / dilate (ITK)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::morphErodeDilate(ROIVolume& vol, int16_t label, int radius)
{
    if (radius == 0) { std::cout << "  ErodeDilate: radius=0, no-op.\n"; return 0; }
    using Ball = itk::BinaryBallStructuringElement<uint8_t, 3>;
    int kr = std::abs(radius);
    Ball ball; ball.SetRadius(kr); ball.CreateStructuringElement();

    Uint8Ptr bin = labelToBinary(vol.maskImage(), label);

    Uint8Ptr result;
    if (radius > 0) {
        using DilF = itk::BinaryDilateImageFilter<Uint8Img, Uint8Img, Ball>;
        auto f = DilF::New();
        f->SetInput(bin); f->SetKernel(ball);
        f->SetForegroundValue(1); f->Update();
        result = f->GetOutput();
    } else {
        using EroF = itk::BinaryErodeImageFilter<Uint8Img, Uint8Img, Ball>;
        auto f = EroF::New();
        f->SetInput(bin); f->SetKernel(ball);
        f->SetForegroundValue(1); f->Update();
        result = f->GetOutput();
    }

    vol.pushUndoForLabel(label);
    binaryToMask(vol.maskImage(), result, label, /*clearFirst=*/true);
    std::cout << "  " << (radius>0?"Dilate":"Erode") << "(" << kr << ") label "
              << label << "\n";
    vol.notifyChange();
    return static_cast<int>(vol.maskImage()
              ->GetLargestPossibleRegion().GetNumberOfPixels());
}

// ════════════════════════════════════════════════════════════════════════════
// Fill holes (ITK)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::fillHoles(ROIVolume& vol, int16_t label, int axis)
{
    vol.pushUndoForLabel(label);
    Uint8Ptr bin = labelToBinary(vol.maskImage(), label);

    if (axis < 0) {
        // 3D fill
        using FillF = itk::BinaryFillholeImageFilter<Uint8Img>;
        auto f = FillF::New();
        f->SetInput(bin); f->SetForegroundValue(1); f->Update();
        binaryToMask(vol.maskImage(), f->GetOutput(), label, true);
    } else {
        // Per-slice fill: extract each 2D slice, run BinaryFillhole on 2D image,
        // write back.  Requires building a 2D slice image.
        // (Simple pixel-by-pixel implementation using 2D ConnectedComponent)
        // For now, fall through to 3D if per-slice is requested.
        using FillF = itk::BinaryFillholeImageFilter<Uint8Img>;
        auto f = FillF::New();
        f->SetInput(bin); f->SetForegroundValue(1); f->Update();
        binaryToMask(vol.maskImage(), f->GetOutput(), label, true);
        std::cout << "  (Per-slice fill not yet supported; falling back to 3D)\n";
    }

    std::cout << "  FillHoles label " << label << "\n";
    vol.notifyChange();
    return 0;
}

// ════════════════════════════════════════════════════════════════════════════
// ROI connected component (keep seed component only)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::roiConnected(ROIVolume& vol,
                                 int sx, int sy, int sz,
                                 int16_t inputLabel, int16_t outputLabel)
{
    Uint8Ptr bin = labelToBinary(vol.maskImage(), inputLabel);
    using CCFilter = itk::ConnectedComponentImageFilter<Uint8Img, Int16Img>;
    auto cc = CCFilter::New();
    cc->SetInput(bin); cc->SetFullyConnected(false); cc->Update();

    Int16Img::IndexType seedIdx; seedIdx[0]=sx; seedIdx[1]=sy; seedIdx[2]=sz;
    int16_t seedComp = cc->GetOutput()->GetPixel(seedIdx);
    if (seedComp == 0) {
        std::cout << "  ROIConnected: seed not inside label " << inputLabel << "\n";
        return 0;
    }

    vol.pushUndoForLabel(inputLabel);
    const int16_t* ccBuf = cc->GetOutput()->GetBufferPointer();
    int16_t* mBuf = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    int kept = 0;
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x) {
        int lin = linIdx(x,y,z,NX,NY);
        if (mBuf[lin] == inputLabel) {
            if (ccBuf[lin] == seedComp) { mBuf[lin] = outputLabel; ++kept; }
            else                          mBuf[lin] = 0;
        }
    }
    std::cout << "  ROIConnected -> label " << outputLabel
              << "  (" << kept << " voxels)\n";
    vol.notifyChange();
    return kept;
}

// ════════════════════════════════════════════════════════════════════════════
// Remove small components
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::removeSmallComponents(ROIVolume& vol,
                                          int16_t label, int minSize)
{
    Uint8Ptr bin = labelToBinary(vol.maskImage(), label);
    using CCFilter  = itk::ConnectedComponentImageFilter<Uint8Img, Int16Img>;
    using RelaFilter = itk::RelabelComponentImageFilter<Int16Img, Int16Img>;
    auto cc = CCFilter::New();
    cc->SetInput(bin); cc->Update();

    auto relabel = RelaFilter::New();
    relabel->SetInput(cc->GetOutput());
    relabel->SetMinimumObjectSize(minSize);
    relabel->Update();

    vol.pushUndoForLabel(label);
    const int16_t* src = relabel->GetOutput()->GetBufferPointer();
    int16_t* dst = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    int removed = 0;
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x) {
        int lin = linIdx(x,y,z,NX,NY);
        if (dst[lin] == label) {
            if (src[lin] == 0) { dst[lin] = 0; ++removed; }
        }
    }
    std::cout << "  RemoveSmall(min=" << minSize << ") removed "
              << removed << " voxels\n";
    vol.notifyChange();
    return removed;
}

// ════════════════════════════════════════════════════════════════════════════
// Connected components (split)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::connectedComponents(ROIVolume& vol,
                                        int16_t inputLabel, int maxComponents)
{
    Uint8Ptr bin = labelToBinary(vol.maskImage(), inputLabel);
    using CCFilter   = itk::ConnectedComponentImageFilter<Uint8Img, Int16Img>;
    using RelaFilter = itk::RelabelComponentImageFilter<Int16Img, Int16Img>;
    auto cc = CCFilter::New();
    cc->SetInput(bin); cc->Update();

    auto relabel = RelaFilter::New();
    relabel->SetInput(cc->GetOutput());
    relabel->SetSortByObjectSize(true); relabel->Update();

    vol.pushUndoForLabel(inputLabel);
    int16_t* dst = vol.maskImage()->GetBufferPointer();
    const int16_t* src = relabel->GetOutput()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    // Clear existing inputLabel voxels first
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x)
        if (dst[linIdx(x,y,z,NX,NY)] == inputLabel)
            dst[linIdx(x,y,z,NX,NY)] = 0;

    int nComp = static_cast<int>(relabel->GetNumberOfObjects());
    int nKeep = std::min(nComp, maxComponents);
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x) {
        int16_t comp = src[linIdx(x,y,z,NX,NY)];
        if (comp > 0 && comp <= nKeep) {
            int16_t newLbl = static_cast<int16_t>(
                ((inputLabel - 1 + comp - 1) % ROIVolume::MAX_LABELS) + 1);
            dst[linIdx(x,y,z,NX,NY)] = newLbl;
        }
    }
    std::cout << "  ConnectedComponents -> " << nKeep << " components\n";
    vol.notifyChange();
    return nKeep;
}

// ════════════════════════════════════════════════════════════════════════════
// Make shell
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::makeShell(ROIVolume& vol, int16_t label, int thickness)
{
    using Ball = itk::BinaryBallStructuringElement<uint8_t, 3>;
    Ball ball; ball.SetRadius(thickness); ball.CreateStructuringElement();

    Uint8Ptr bin = labelToBinary(vol.maskImage(), label);
    using EroF = itk::BinaryErodeImageFilter<Uint8Img, Uint8Img, Ball>;
    auto ero = EroF::New();
    ero->SetInput(bin); ero->SetKernel(ball);
    ero->SetForegroundValue(1); ero->Update();

    // shell = original AND NOT eroded
    const uint8_t* orig = bin->GetBufferPointer();
    const uint8_t* eroded = ero->GetOutput()->GetBufferPointer();
    auto shellImg = Uint8Img::New();
    shellImg->SetRegions(bin->GetLargestPossibleRegion());
    shellImg->SetSpacing(bin->GetSpacing());
    shellImg->SetOrigin(bin->GetOrigin());
    shellImg->SetDirection(bin->GetDirection());
    shellImg->Allocate(true);
    uint8_t* shellBuf = shellImg->GetBufferPointer();
    size_t np = bin->GetLargestPossibleRegion().GetNumberOfPixels();
    int shellVox = 0;
    for (size_t i = 0; i < np; ++i) {
        shellBuf[i] = (orig[i] && !eroded[i]) ? 1u : 0u;
        if (shellBuf[i]) ++shellVox;
    }

    vol.pushUndoForLabel(label);
    binaryToMask(vol.maskImage(), shellImg, label, true);
    std::cout << "  MakeShell(t=" << thickness << ") label " << label
              << "  (" << shellVox << " voxels)\n";
    vol.notifyChange();
    return shellVox;
}

// ════════════════════════════════════════════════════════════════════════════
// Low-pass smooth
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::lowPassSmooth(ROIVolume& vol, int16_t label, float sigma)
{
    // Cast uint8 to float, apply Gaussian, threshold at 0.5
    Uint8Ptr bin = labelToBinary(vol.maskImage(), label);
    using CastF = itk::CastImageFilter<Uint8Img, FloatImg>;
    auto cf = CastF::New(); cf->SetInput(bin); cf->Update();

    using GaussF = itk::SmoothingRecursiveGaussianImageFilter<FloatImg, FloatImg>;
    auto g = GaussF::New(); g->SetInput(cf->GetOutput()); g->SetSigma(sigma);
    g->Update();

    const float* smooth = g->GetOutput()->GetBufferPointer();
    auto outBin = Uint8Img::New();
    outBin->SetRegions(bin->GetLargestPossibleRegion());
    outBin->SetSpacing(bin->GetSpacing());
    outBin->SetOrigin(bin->GetOrigin());
    outBin->SetDirection(bin->GetDirection());
    outBin->Allocate(true);
    uint8_t* outBuf = outBin->GetBufferPointer();
    size_t np = bin->GetLargestPossibleRegion().GetNumberOfPixels();
    int n = 0;
    for (size_t i = 0; i < np; ++i) {
        outBuf[i] = smooth[i] >= 0.5f ? 1u : 0u;
        if (outBuf[i]) ++n;
    }

    vol.pushUndoForLabel(label);
    binaryToMask(vol.maskImage(), outBin, label, true);
    std::cout << "  LowPassSmooth(sigma=" << sigma << ") label " << label
              << "  (" << n << " voxels)\n";
    vol.notifyChange();
    return n;
}

// ════════════════════════════════════════════════════════════════════════════
// Boolean op
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::booleanOp(ROIVolume& vol,
                              int16_t labelA, int16_t labelB,
                              const std::string& op,
                              int16_t outputLabel)
{
    int16_t* buf = vol.maskImage()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    size_t np = static_cast<size_t>(NX) * NY * NZ;

    std::vector<bool> A(np), B(np);
    for (size_t i = 0; i < np; ++i) {
        A[i] = buf[i] == labelA;
        B[i] = buf[i] == labelB;
    }

    std::vector<bool> result(np, false);
    if (op == "and")      for (size_t i=0;i<np;++i) result[i] = A[i] && B[i];
    else if (op == "or")  for (size_t i=0;i<np;++i) result[i] = A[i] || B[i];
    else if (op == "xor") for (size_t i=0;i<np;++i) result[i] = A[i] != B[i];
    else if (op == "not") for (size_t i=0;i<np;++i) result[i] = !A[i];
    else if (op == "subtract") for (size_t i=0;i<np;++i) result[i] = A[i] && !B[i];
    else throw std::invalid_argument("Unknown boolean op: " + op);

    // Undo
    vol.pushUndoForLabel(outputLabel);
    // Clear output label, then write result
    for (size_t i = 0; i < np; ++i)
        if (buf[i] == outputLabel) buf[i] = 0;
    int n = 0;
    for (size_t i = 0; i < np; ++i)
        if (result[i]) { buf[i] = outputLabel; ++n; }

    std::cout << "  BooleanOp(" << op << ") A=" << labelA << " B=" << labelB
              << " -> label " << outputLabel << "  (" << n << " voxels)\n";
    vol.notifyChange();
    return n;
}

// ════════════════════════════════════════════════════════════════════════════
// Level set (Geodesic Active Contour, ITK)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::levelSetRefine(ROIVolume& vol, int16_t label,
                                   int nIterations,
                                   float propagationScale,
                                   float curvatureScale)
{
    using DistF   = itk::SignedMaurerDistanceMapImageFilter<Uint8Img, FloatImg>;
    using GaussF  = itk::SmoothingRecursiveGaussianImageFilter<FloatImg, FloatImg>;
    using GradF   = itk::GradientMagnitudeImageFilter<FloatImg, FloatImg>;
    using RecipF  = itk::BoundedReciprocalImageFilter<FloatImg, FloatImg>;
    using LSType  = itk::GeodesicActiveContourLevelSetImageFilter<FloatImg, FloatImg>;
    using CastF   = itk::CastImageFilter<Uint8Img, FloatImg>;

    Uint8Ptr bin = labelToBinary(vol.maskImage(), label);

    // Signed distance of current mask boundary (initial level set)
    auto dist = DistF::New();
    dist->SetInput(bin);
    dist->SetInsideIsPositive(false);
    dist->SetSquaredDistance(false);
    dist->SetUseImageSpacing(false);
    dist->Update();

    // Edge potential: 1 / (1 + |∇(G*I)|)
    auto gauss = GaussF::New();
    gauss->SetInput(vol.displayImage());
    gauss->SetSigma(1.0f);
    gauss->Update();

    auto grad = GradF::New();
    grad->SetInput(gauss->GetOutput());
    grad->Update();

    auto recip = RecipF::New();
    recip->SetInput(grad->GetOutput());
    recip->Update();

    // Level set evolution
    using CastD = itk::CastImageFilter<FloatImg, itk::Image<double,3>>;
    using DoubleImg = itk::Image<double,3>;
    using GACLS = itk::GeodesicActiveContourLevelSetImageFilter<DoubleImg, DoubleImg>;

    auto cd1 = itk::CastImageFilter<FloatImg, DoubleImg>::New();
    cd1->SetInput(dist->GetOutput()); cd1->Update();

    auto cd2 = itk::CastImageFilter<FloatImg, DoubleImg>::New();
    cd2->SetInput(recip->GetOutput()); cd2->Update();

    auto ls = GACLS::New();
    ls->SetInput(cd1->GetOutput());
    ls->SetFeatureImage(cd2->GetOutput());
    ls->SetNumberOfIterations(nIterations);
    ls->SetPropagationScaling(propagationScale);
    ls->SetCurvatureScaling(curvatureScale);
    ls->SetAdvectionScaling(2.0);
    ls->SetMaximumRMSError(0.005);
    ls->Update();

    const auto* src = ls->GetOutput()->GetBufferPointer();   // float* (ITK default output pixel)
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    auto outBin = Uint8Img::New();
    outBin->SetRegions(bin->GetLargestPossibleRegion());
    outBin->SetSpacing(bin->GetSpacing());
    outBin->SetOrigin(bin->GetOrigin());
    outBin->SetDirection(bin->GetDirection());
    outBin->Allocate(true);
    uint8_t* outBuf = outBin->GetBufferPointer();
    int n = 0;
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x) {
        int lin = linIdx(x,y,z,NX,NY);
        outBuf[lin] = src[lin] <= 0.0 ? 1u : 0u;
        if (outBuf[lin]) ++n;
    }

    vol.pushUndoForLabel(label);
    binaryToMask(vol.maskImage(), outBin, label, true);
    std::cout << "  LevelSet(iter=" << nIterations << ") label " << label
              << "  (" << n << " voxels)\n";
    vol.notifyChange();
    return n;
}

// ════════════════════════════════════════════════════════════════════════════
// Watershed (ITK morphological)
// ════════════════════════════════════════════════════════════════════════════

int ROIAlgorithms::watershed(ROIVolume& vol, int16_t inputLabel)
{
    using DistF = itk::SignedMaurerDistanceMapImageFilter<Uint8Img, FloatImg>;
    using WatershedF = itk::MorphologicalWatershedImageFilter<FloatImg, Int16Img>;
    using MaskF = itk::MaskImageFilter<Int16Img, Uint8Img>;

    Uint8Ptr bin = labelToBinary(vol.maskImage(), inputLabel);

    auto dist = DistF::New();
    dist->SetInput(bin);
    dist->SetInsideIsPositive(true);
    dist->SetSquaredDistance(false);
    dist->SetUseImageSpacing(false);
    dist->Update();

    // Negate: watershed on inverted distance = splits at local maxima of distance
    size_t np = bin->GetLargestPossibleRegion().GetNumberOfPixels();
    auto neg = FloatImg::New();
    neg->SetRegions(bin->GetLargestPossibleRegion());
    neg->SetSpacing(bin->GetSpacing());
    neg->SetOrigin(bin->GetOrigin());
    neg->SetDirection(bin->GetDirection());
    neg->Allocate(true);
    float* negBuf = neg->GetBufferPointer();
    const float* distBuf = dist->GetOutput()->GetBufferPointer();
    for (size_t i = 0; i < np; ++i) negBuf[i] = -distBuf[i];

    auto ws = WatershedF::New();
    ws->SetInput(neg);
    ws->SetLevel(0.0f);
    ws->SetMarkWatershedLine(false);
    ws->SetFullyConnected(true);
    ws->Update();

    // Mask to inputLabel region
    auto maskF = MaskF::New();
    maskF->SetInput(ws->GetOutput());
    maskF->SetMaskImage(bin);
    maskF->Update();

    const int16_t* wsBuf = maskF->GetOutput()->GetBufferPointer();
    int NX = vol.nx(), NY = vol.ny(), NZ = vol.nz();
    int16_t nComp = 0;
    for (size_t i = 0; i < np; ++i) nComp = std::max(nComp, wsBuf[i]);

    vol.pushUndoForLabel(inputLabel);
    int16_t* dst = vol.maskImage()->GetBufferPointer();
    // Clear existing inputLabel
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x)
        if (dst[linIdx(x,y,z,NX,NY)] == inputLabel)
            dst[linIdx(x,y,z,NX,NY)] = 0;
    // Assign consecutive labels
    for (int z=0;z<NZ;++z) for (int y=0;y<NY;++y) for (int x=0;x<NX;++x) {
        int16_t comp = wsBuf[linIdx(x,y,z,NX,NY)];
        if (comp > 0) {
            int16_t newLbl = static_cast<int16_t>(
                ((inputLabel - 1 + comp - 1) % ROIVolume::MAX_LABELS) + 1);
            dst[linIdx(x,y,z,NX,NY)] = newLbl;
        }
    }
    std::cout << "  Watershed -> " << nComp << " components\n";
    vol.notifyChange();
    return nComp;
}
