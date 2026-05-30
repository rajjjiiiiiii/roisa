#pragma once
// ROIAlgorithms.h — All segmentation and morphological operations
//
// Every function takes an ROIVolume by reference, modifies the mask in place
// (with undo recorded), and returns the number of voxels affected (-1 on error).

#include "ROIVolume.h"

namespace ROIAlgorithms {

// ════════════════════════════════════════════════════════════════════════════
// Brush (interactive paint)
// ════════════════════════════════════════════════════════════════════════════

/// Paint a brush footprint centred at (cx, cy, cz).
/// shape: 0 = sphere, 1 = cylinder, 2 = cube
/// viewAxis: 0=X, 1=Y, 2=Z  (used to orient cylinder; clips twoD mode)
/// twoD: restrict to current viewing slice only
/// label = 0 → erase
int paintBrush(ROIVolume& vol,
               int cx, int cy, int cz,
               int radius, int16_t label,
               int brushShape = 0,
               int viewAxis   = 2,
               bool twoD      = false);

// ════════════════════════════════════════════════════════════════════════════
// Intensity-based
// ════════════════════════════════════════════════════════════════════════════

/// Paint all voxels with intensity in [lower, upper] with label.
/// Restrict to a single slice by passing axis >= 0 and sliceIdx >= 0.
int thresholdSegment(ROIVolume& vol,
                     float lower, float upper, int16_t label,
                     int axis = -1, int sliceIdx = -1);

/// Otsu automatic threshold (binary: nClasses=1; multi: nClasses > 1).
/// Multi-class assigns consecutive labels from `label`.
int otsuThreshold(ROIVolume& vol, int16_t label,
                  int nBins = 128, int nClasses = 1);

/// K-means intensity clustering; assigns labels label, label+1, …, label+k-1.
int kMeansCluster(ROIVolume& vol, int k, int16_t labelOffset = 1);

// ════════════════════════════════════════════════════════════════════════════
// Seed-based / region growing
// ════════════════════════════════════════════════════════════════════════════

/// BFS region grow within ±tolerance of seed intensity.
int regionGrow(ROIVolume& vol,
               int sx, int sy, int sz,
               float tolerance, int16_t label,
               int maxVoxels = 500000);

/// ITK ConnectedThreshold from a seed.
int connectedThreshold(ROIVolume& vol,
                       int sx, int sy, int sz,
                       float lower, float upper, int16_t label);

/// ITK NeighborhoodConnected: all neighbours in radius must satisfy [lo,hi].
int neighborhoodConnected(ROIVolume& vol,
                          int sx, int sy, int sz,
                          float lower, float upper,
                          int radius, int16_t label);

/// ITK ConfidenceConnected: local mean ± multiplier*sigma.
int confidenceConnected(ROIVolume& vol,
                        int sx, int sy, int sz,
                        float multiplier, int iterations,
                        int radius, int16_t label);

/// 2D paint-bucket flood fill in the slice defined by (sx,sy,sz) and viewAxis.
int floodFill2D(ROIVolume& vol,
                int sx, int sy, int sz,
                int viewAxis, float tolerance, int16_t label);

/// ITK FastMarchingImageFilter: wave front from seed, stop at stoppingValue.
int fastMarching(ROIVolume& vol,
                 int sx, int sy, int sz,
                 float stoppingValue, int16_t label);

// ════════════════════════════════════════════════════════════════════════════
// Morphological operations
// ════════════════════════════════════════════════════════════════════════════

/// Erode (radius < 0) or dilate (radius > 0) the given label.
int morphErodeDilate(ROIVolume& vol, int16_t label, int radius);

/// Fill enclosed holes in label (axis -1 = 3D; 0/1/2 = per-slice).
int fillHoles(ROIVolume& vol, int16_t label, int axis = -1);

/// Keep only the connected component of label that contains (sx,sy,sz).
int roiConnected(ROIVolume& vol, int sx, int sy, int sz,
                 int16_t inputLabel, int16_t outputLabel);

/// Remove components of label smaller than minSize voxels.
int removeSmallComponents(ROIVolume& vol, int16_t label, int minSize);

/// Split label into connected components; assign consecutive labels from label.
int connectedComponents(ROIVolume& vol, int16_t inputLabel,
                        int maxComponents = 255);

/// Replace label with hollow shell of given voxel thickness.
int makeShell(ROIVolume& vol, int16_t label, int thickness = 1);

/// Gaussian smooth then rethreshold at 0.5 to smooth the boundary.
int lowPassSmooth(ROIVolume& vol, int16_t label, float sigma = 1.0f);

/// Boolean combine two labels: op = "and","or","xor","not","subtract".
int booleanOp(ROIVolume& vol,
              int16_t labelA, int16_t labelB,
              const std::string& op,
              int16_t outputLabel);

// ════════════════════════════════════════════════════════════════════════════
// Advanced segmentation
// ════════════════════════════════════════════════════════════════════════════

/// Geodesic Active Contour level-set boundary refinement.
int levelSetRefine(ROIVolume& vol, int16_t label,
                   int   nIterations      = 500,
                   float propagationScale = 1.0f,
                   float curvatureScale   = 1.0f);

/// Morphological watershed split of label; assigns consecutive labels.
int watershed(ROIVolume& vol, int16_t inputLabel);

} // namespace ROIAlgorithms
