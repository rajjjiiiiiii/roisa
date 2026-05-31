"""
suv.py — PET SUV (Standardized Uptake Value) quantification.

Pure functions + a parameter dataclass, independent of Qt.  Used by the
Quantification operator to convert PET activity-concentration voxels
(Bq/mL) into SUV and to compute per-ROI SUV statistics and time-activity
curves.

SUV definitions
---------------
    SUVbw  = activity_conc[Bq/mL] * body_mass[g]      / dose_at_scan[Bq]
    SUVlbm = activity_conc[Bq/mL] * lean_body_mass[g] / dose_at_scan[Bq]

dose_at_scan is the injected dose decay-corrected from injection time to
scan time:   dose_at_scan = dose_injected * 2^(-elapsed / half_life)
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import List, Optional

import numpy as np

F18_HALF_LIFE_S = 6586.2   # Fluorine-18 half-life (seconds)


@dataclass
class SUVParams:
    suv_type:    int   = 0          # 0 = body weight, 1 = lean body mass
    weight_kg:   float = 70.0
    height_cm:   float = 170.0      # for LBM
    sex:         int   = 0          # 0 = male, 1 = female (for LBM)
    dose_mbq:    float = 370.0      # injected dose (MBq)
    half_life_s: float = F18_HALF_LIFE_S
    decay_min:   float = 60.0       # elapsed injection -> scan (minutes)
    from_dicom:  bool  = False      # were these auto-filled?

    def body_mass_g(self) -> float:
        if self.suv_type == 1:
            return lean_body_mass_kg(self.weight_kg, self.height_cm, self.sex) * 1000.0
        return self.weight_kg * 1000.0


def lean_body_mass_kg(weight_kg: float, height_cm: float, sex: int) -> float:
    """James formula lean body mass.  sex: 0=male, 1=female."""
    if height_cm <= 0:
        return weight_kg
    w, h = weight_kg, height_cm
    if sex == 1:   # female
        return 1.07 * w - 148.0 * (w / h) ** 2
    return 1.10 * w - 128.0 * (w / h) ** 2


def decay_corrected_dose_bq(p: SUVParams) -> float:
    dose_bq = p.dose_mbq * 1.0e6
    if p.half_life_s <= 0:
        return dose_bq
    decay = math.pow(2.0, -(p.decay_min * 60.0) / p.half_life_s)
    return dose_bq * decay


def suv_factor(p: SUVParams) -> float:
    """Multiplier converting activity concentration (Bq/mL) into SUV."""
    dose = decay_corrected_dose_bq(p)
    if dose <= 0:
        return 0.0
    return p.body_mass_g() / dose


def _hhmmss_to_seconds(t: str) -> Optional[float]:
    """Parse a DICOM TM value 'HHMMSS.frac' into seconds since midnight."""
    if not t:
        return None
    try:
        t = t.strip()
        hh = int(t[0:2]); mm = int(t[2:4]); ss = float(t[4:]) if len(t) > 4 else 0.0
        return hh * 3600 + mm * 60 + ss
    except (ValueError, IndexError):
        return None


def extract_suv_params(dicom_path: str) -> Optional[SUVParams]:
    """Best-effort SUV parameter extraction from a DICOM file via pydicom.

    Returns None if pydicom is unavailable or the file can't be read.
    Missing fields keep their SUVParams defaults.
    """
    if not dicom_path:
        return None
    try:
        import pydicom
    except ImportError:
        return None
    try:
        ds = pydicom.dcmread(dicom_path, stop_before_pixels=True, force=True)
    except Exception:
        return None

    p = SUVParams(from_dicom=True)
    if "PatientWeight" in ds and ds.PatientWeight:
        p.weight_kg = float(ds.PatientWeight)
    if "PatientSize" in ds and ds.PatientSize:
        try:
            p.height_cm = float(ds.PatientSize) * 100.0   # DICOM stores metres
        except (ValueError, TypeError):
            pass
    if "PatientSex" in ds:
        p.sex = 1 if str(ds.PatientSex).upper().startswith("F") else 0

    inj_s = None
    try:
        seq = ds.RadiopharmaceuticalInformationSequence[0]
        if "RadionuclideTotalDose" in seq and seq.RadionuclideTotalDose:
            p.dose_mbq = float(seq.RadionuclideTotalDose) / 1.0e6   # Bq -> MBq
        if "RadionuclideHalfLife" in seq and seq.RadionuclideHalfLife:
            p.half_life_s = float(seq.RadionuclideHalfLife)
        if "RadiopharmaceuticalStartTime" in seq:
            inj_s = _hhmmss_to_seconds(str(seq.RadiopharmaceuticalStartTime))
    except (AttributeError, IndexError, KeyError):
        pass

    scan_s = _hhmmss_to_seconds(str(ds.get("SeriesTime", "")
                                     or ds.get("AcquisitionTime", "")))
    if inj_s is not None and scan_s is not None:
        elapsed = scan_s - inj_s
        if elapsed < 0:                 # crossed midnight
            elapsed += 24 * 3600
        p.decay_min = elapsed / 60.0
    return p


# ── ROI statistics ──────────────────────────────────────────────────────────────

def _peak_radius_voxels(spacing_xyz) -> tuple:
    """Voxel radius approximating a 0.5 cm sphere for SUVpeak averaging."""
    return tuple(max(1, int(round(5.0 / max(s, 1e-3)))) for s in spacing_xyz)


def roi_suv_stats(activity: np.ndarray, mask: np.ndarray, label: int,
                  spacing_xyz, factor: float) -> Optional[dict]:
    """Per-ROI SUV statistics.

    activity : (nz,ny,nx) PET activity concentration, aligned to `mask`'s grid
    mask     : (nz,ny,nx) int label volume
    factor   : SUV multiplier (suv_factor(params))
    """
    sel = (mask == label)
    n = int(sel.sum())
    if n == 0 or activity.shape != mask.shape:
        return None
    sx, sy, sz = spacing_xyz
    vox_ml = (sx * sy * sz) / 1000.0
    suv = activity.astype(np.float32) * factor
    vals = suv[sel]
    suv_mean = float(vals.mean())
    suv_max  = float(vals.max())

    # SUVpeak — mean over a small sphere around the hottest voxel in the ROI
    flat_idx = np.argmax(np.where(sel, suv, -np.inf))
    cz, cy, cx = np.unravel_index(flat_idx, suv.shape)
    rz, ry, rx = _peak_radius_voxels((sz, sy, sx))
    z0, z1 = max(0, cz - rz), min(suv.shape[0], cz + rz + 1)
    y0, y1 = max(0, cy - ry), min(suv.shape[1], cy + ry + 1)
    x0, x1 = max(0, cx - rx), min(suv.shape[2], cx + rx + 1)
    suv_peak = float(suv[z0:z1, y0:y1, x0:x1].mean())

    vol_ml = n * vox_ml
    return {
        "label":    label,
        "voxels":   n,
        "volume_ml": vol_ml,
        "suv_mean": suv_mean,
        "suv_max":  suv_max,
        "suv_peak": suv_peak,
        "tlg":      suv_mean * vol_ml,   # total lesion glycolysis
    }


def time_activity_curve(frames: List[np.ndarray], mask: np.ndarray,
                        label: int, factor: float) -> List[float]:
    """Mean SUV within `label` for each frame (each aligned to mask grid)."""
    sel = (mask == label)
    out: List[float] = []
    if sel.sum() == 0:
        return out
    for fr in frames:
        if fr is None or fr.shape != mask.shape:
            out.append(float("nan"))
            continue
        out.append(float(fr[sel].mean()) * factor)
    return out
