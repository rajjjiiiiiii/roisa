#pragma once
// SUV.h — PET Standardized Uptake Value quantification (Qt-independent).
//
//   SUVbw  = activity_conc[Bq/mL] * body_mass[g]      / dose_at_scan[Bq]
//   SUVlbm = activity_conc[Bq/mL] * lean_body_mass[g] / dose_at_scan[Bq]
//
// dose_at_scan = injected_dose * 2^(-elapsed / half_life)

#include <cstdint>
#include <string>
#include <vector>

constexpr double F18_HALF_LIFE_S = 6586.2;   // Fluorine-18 half-life (seconds)

struct SUVParams {
    int    suvType   {0};         // 0 = body weight, 1 = lean body mass
    double weightKg  {70.0};
    double heightCm  {170.0};     // for LBM
    int    sex       {0};         // 0 = male, 1 = female (for LBM)
    double doseMbq   {370.0};     // injected dose (MBq)
    double halfLifeS {F18_HALF_LIFE_S};
    double decayMin  {60.0};      // elapsed injection -> scan (minutes)
    bool   fromDicom {false};

    double bodyMassG() const;     // grams (body weight or lean body mass)
};

struct ROISUVStats {
    int    label   {0};
    int    voxels  {0};
    double volumeMl{0.0};
    double suvMean {0.0};
    double suvMax  {0.0};
    double suvPeak {0.0};
    double tlg     {0.0};
};

namespace SUV {

double leanBodyMassKg(double weightKg, double heightCm, int sex);
double decayCorrectedDoseBq(const SUVParams& p);
double factor(const SUVParams& p);

/// Best-effort SUV parameter extraction from a DICOM file (top-level tags).
/// `ok` is set true if any tag was read.  Missing fields keep defaults.
SUVParams extractParams(const std::string& dicomFile, bool& ok);

/// Per-ROI SUV stats over an activity buffer aligned to the mask grid.
/// Buffers are x-fastest (lin = x + nx*y + nx*ny*z).  Returns false if empty.
bool roiStats(const float* activity, const int16_t* mask,
              int nx, int ny, int nz, int label,
              double sx, double sy, double sz, double suvFactor,
              ROISUVStats& out);

/// Mean SUV within `label` for each frame (each aligned to the mask grid).
std::vector<double> tac(const std::vector<const float*>& frames,
                        const int16_t* mask, int nx, int ny, int nz,
                        int label, double suvFactor);

// ── ROI analysis ────────────────────────────────────────────────────────────────

/// PERCIST-style threshold: write `targetLabel` into `mask` for all voxels whose
/// `activity` ≥ pct% of the peak (peak = max within `sourceLabel`, or whole image
/// when sourceLabel == 0).  Returns the voxel count written, or -1 on empty source.
long percentThreshold(const float* activity, int16_t* mask,
                      int nx, int ny, int nz,
                      int sourceLabel, double pct, int targetLabel);

/// (meanA, meanB, ratio) for two labels.  Returns false if either is empty.
bool roiRatio(const float* activity, const int16_t* mask, int nx, int ny, int nz,
              int labelA, int labelB,
              double& meanA, double& meanB, double& ratio);

/// Histogram of `activity` within `label` (nbins bins between min and max).
/// Returns false if the label is empty.
bool roiHistogram(const float* activity, const int16_t* mask, int nx, int ny, int nz,
                  int label, int nbins,
                  std::vector<double>& counts, double& vmin, double& vmax);

// ── Kinetic modeling (Patlak / Logan graphical analysis) ───────────────────────

struct KineticResult {
    bool        ok{false};
    std::string model;    // "Patlak" | "Logan"
    std::string param;    // "Ki"     | "DVR"
    double      slope{0.0};
    double      intercept{0.0};
    std::vector<double> y;   // linearized y-values for plotting
};

/// tissue = target ROI TAC, input = blood-pool ROI TAC, dtMin = frame step.
KineticResult patlak(const std::vector<double>& tissue,
                     const std::vector<double>& input, double dtMin, int fitFrom);
KineticResult logan (const std::vector<double>& tissue,
                     const std::vector<double>& input, double dtMin, int fitFrom);

} // namespace SUV
