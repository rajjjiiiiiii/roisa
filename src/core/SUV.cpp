// SUV.cpp — PET SUV quantification implementation.

#include "SUV.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <itkGDCMImageIO.h>
#include <itkMetaDataObject.h>

// ── Parameters ──────────────────────────────────────────────────────────────────

double SUVParams::bodyMassG() const
{
    if (suvType == 1)
        return SUV::leanBodyMassKg(weightKg, heightCm, sex) * 1000.0;
    return weightKg * 1000.0;
}

namespace SUV {

double leanBodyMassKg(double weightKg, double heightCm, int sex)
{
    if (heightCm <= 0.0) return weightKg;
    const double w = weightKg, h = heightCm;
    if (sex == 1)   // female (James formula)
        return 1.07 * w - 148.0 * (w / h) * (w / h);
    return 1.10 * w - 128.0 * (w / h) * (w / h);   // male
}

double decayCorrectedDoseBq(const SUVParams& p)
{
    const double doseBq = p.doseMbq * 1.0e6;
    if (p.halfLifeS <= 0.0) return doseBq;
    const double decay = std::pow(2.0, -(p.decayMin * 60.0) / p.halfLifeS);
    return doseBq * decay;
}

double factor(const SUVParams& p)
{
    const double dose = decayCorrectedDoseBq(p);
    if (dose <= 0.0) return 0.0;
    return p.bodyMassG() / dose;
}

// ── DICOM extraction (best-effort, top-level tags) ──────────────────────────────

static std::string trimTag(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0' || s.back() == '\r'
                          || s.back() == '\n'))
        s.pop_back();
    size_t b = s.find_first_not_of(' ');
    return (b == std::string::npos) ? std::string() : s.substr(b);
}

static bool readTag(const itk::MetaDataDictionary& dict,
                    const char* key, std::string& out)
{
    std::string raw;
    if (!itk::ExposeMetaData<std::string>(dict, key, raw)) return false;
    out = trimTag(raw);
    return !out.empty();
}

// Parse a DICOM TM value "HHMMSS.frac" → seconds since midnight (−1 on failure).
static double hhmmssToSeconds(const std::string& t)
{
    if (t.size() < 4) return -1.0;
    try {
        int hh = std::stoi(t.substr(0, 2));
        int mm = std::stoi(t.substr(2, 2));
        double ss = (t.size() > 4) ? std::stod(t.substr(4)) : 0.0;
        return hh * 3600 + mm * 60 + ss;
    } catch (...) { return -1.0; }
}

SUVParams extractParams(const std::string& dicomFile, bool& ok)
{
    SUVParams p;
    ok = false;
    if (dicomFile.empty()) return p;
    try {
        auto io = itk::GDCMImageIO::New();
        io->SetFileName(dicomFile);
        io->ReadImageInformation();
        const auto& dict = io->GetMetaDataDictionary();

        std::string v;
        if (readTag(dict, "0010|1030", v)) { p.weightKg = std::stod(v); ok = true; }
        if (readTag(dict, "0010|1020", v)) { p.heightCm = std::stod(v) * 100.0; ok = true; }
        if (readTag(dict, "0010|0040", v)) { p.sex = (v[0]=='F'||v[0]=='f') ? 1 : 0; ok = true; }
        // Radiopharmaceutical tags — present at top level on some exports
        if (readTag(dict, "0018|1074", v)) { p.doseMbq = std::stod(v) / 1.0e6; ok = true; }
        if (readTag(dict, "0018|1075", v)) { p.halfLifeS = std::stod(v); ok = true; }

        std::string injStr, scanStr;
        readTag(dict, "0018|1072", injStr);                      // inj start time
        if (!readTag(dict, "0008|0031", scanStr))                // series time
            readTag(dict, "0008|0032", scanStr);                 // acquisition time
        double inj = hhmmssToSeconds(injStr), scan = hhmmssToSeconds(scanStr);
        if (inj >= 0 && scan >= 0) {
            double elapsed = scan - inj;
            if (elapsed < 0) elapsed += 24 * 3600;
            p.decayMin = elapsed / 60.0;
            ok = true;
        }
    } catch (...) {
        // leave defaults; ok stays as-is
    }
    p.fromDicom = ok;
    return p;
}

// ── ROI statistics ──────────────────────────────────────────────────────────────

bool roiStats(const float* activity, const int16_t* mask,
              int nx, int ny, int nz, int label,
              double sx, double sy, double sz, double suvFactor,
              ROISUVStats& out)
{
    if (!activity || !mask) return false;
    const long n = static_cast<long>(nx) * ny * nz;

    long count = 0;
    double sum = 0.0;
    double smax = -std::numeric_limits<double>::infinity();
    long   maxLin = -1;
    for (long i = 0; i < n; ++i) {
        if (mask[i] != label) continue;
        const double suv = activity[i] * suvFactor;
        sum += suv;
        ++count;
        if (suv > smax) { smax = suv; maxLin = i; }
    }
    if (count == 0) return false;

    out.label   = label;
    out.voxels  = static_cast<int>(count);
    const double voxMl = (sx * sy * sz) / 1000.0;
    out.volumeMl = count * voxMl;
    out.suvMean  = sum / count;
    out.suvMax   = smax;

    // SUVpeak — mean SUV over a ~0.5 cm sphere around the hottest voxel
    const int rx = std::max(1, (int)std::lround(5.0 / std::max(sx, 1e-3)));
    const int ry = std::max(1, (int)std::lround(5.0 / std::max(sy, 1e-3)));
    const int rz = std::max(1, (int)std::lround(5.0 / std::max(sz, 1e-3)));
    const int cx = maxLin % nx;
    const int cy = (maxLin / nx) % ny;
    const int cz = maxLin / (nx * ny);
    double psum = 0.0; long pcount = 0;
    for (int z = std::max(0, cz-rz); z < std::min(nz, cz+rz+1); ++z)
    for (int y = std::max(0, cy-ry); y < std::min(ny, cy+ry+1); ++y)
    for (int x = std::max(0, cx-rx); x < std::min(nx, cx+rx+1); ++x) {
        psum += activity[x + nx*y + nx*ny*z] * suvFactor;
        ++pcount;
    }
    out.suvPeak = (pcount > 0) ? psum / pcount : out.suvMax;
    out.tlg     = out.suvMean * out.volumeMl;
    return true;
}

std::vector<double> tac(const std::vector<const float*>& frames,
                        const int16_t* mask, int nx, int ny, int nz,
                        int label, double suvFactor)
{
    std::vector<double> out;
    if (!mask) return out;
    const long n = static_cast<long>(nx) * ny * nz;

    // ROI must be non-empty
    long roiCount = 0;
    for (long i = 0; i < n; ++i) if (mask[i] == label) ++roiCount;
    if (roiCount == 0) return out;

    for (const float* fr : frames) {
        if (!fr) { out.push_back(std::numeric_limits<double>::quiet_NaN()); continue; }
        double sum = 0.0; long c = 0;
        for (long i = 0; i < n; ++i)
            if (mask[i] == label) { sum += fr[i]; ++c; }
        out.push_back((c > 0) ? (sum / c) * suvFactor : 0.0);
    }
    return out;
}

} // namespace SUV
