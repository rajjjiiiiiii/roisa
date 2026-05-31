"""
analysis.py — ROI analysis helpers (Qt-independent).

Pure NumPy functions used by the Quantification operator:
  * percent_threshold  — PERCIST-style "voxels >= X% of max" selection
  * roi_ratio          — mean ratio between two labels (lesion-to-background)
  * roi_histogram      — intensity/SUV histogram within one label
"""

from __future__ import annotations

from typing import Optional, Tuple

import numpy as np


def percent_threshold(activity: np.ndarray, mask: np.ndarray,
                      source_label: int, pct: float) -> Optional[np.ndarray]:
    """Boolean volume of voxels >= pct% of the peak.

    The peak is the maximum of `activity` within `source_label` (or the whole
    image when source_label == 0).  The threshold is then applied across the
    whole image — the classic PERCIST workflow of seeding from a reference VOI.
    Returns None if the source region is empty.
    """
    if activity is None or mask is None or activity.shape != mask.shape:
        return None
    if source_label > 0:
        region = activity[mask == source_label]
        if region.size == 0:
            return None
        peak = float(region.max())
    else:
        peak = float(activity.max())
    if peak <= 0:
        return None
    return activity >= (pct / 100.0) * peak


def roi_ratio(activity: np.ndarray, mask: np.ndarray,
              label_a: int, label_b: int) -> Optional[Tuple[float, float, float]]:
    """(mean_a, mean_b, mean_a / mean_b) for two labels; None if either empty."""
    if activity is None or mask is None or activity.shape != mask.shape:
        return None
    a = activity[mask == label_a]
    b = activity[mask == label_b]
    if a.size == 0 or b.size == 0:
        return None
    mean_a, mean_b = float(a.mean()), float(b.mean())
    ratio = mean_a / mean_b if abs(mean_b) > 1e-9 else float("inf")
    return mean_a, mean_b, ratio


def roi_histogram(activity: np.ndarray, mask: np.ndarray, label: int,
                  bins: int = 64) -> Optional[Tuple[np.ndarray, float, float]]:
    """(counts, vmin, vmax) of `activity` values inside `label`; None if empty."""
    if activity is None or mask is None or activity.shape != mask.shape:
        return None
    vals = activity[mask == label]
    if vals.size == 0:
        return None
    vmin, vmax = float(vals.min()), float(vals.max())
    if vmax - vmin < 1e-9:
        vmax = vmin + 1.0
    counts, _ = np.histogram(vals, bins=bins, range=(vmin, vmax))
    return counts.astype(np.float64), vmin, vmax
