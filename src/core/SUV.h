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

} // namespace SUV
