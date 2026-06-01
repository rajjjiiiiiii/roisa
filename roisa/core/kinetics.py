"""
kinetics.py — Graphical kinetic analysis from a multi-frame TAC (Qt-independent).

Patlak  (irreversible tracers):
    y = Ct(t) / Cp(t)
    x = ∫₀ᵗ Cp dτ / Cp(t)
    late-time slope  Ki  (influx rate constant)

Logan   (reversible tracers):
    y = ∫₀ᵗ Ct dτ / Ct(t)
    x = ∫₀ᵗ Cp dτ / Ct(t)
    late-time slope  DVR / Vt  (distribution volume ratio)

Ct = tissue (target ROI) TAC, Cp = input-function (blood-pool ROI) TAC.
Frame timing is taken as uniform with step dt_min (minutes).
"""

from __future__ import annotations

from typing import List, Optional, Tuple

import numpy as np


def _cumtrapz(y: np.ndarray, dt: float) -> np.ndarray:
    """Cumulative trapezoidal integral with a leading zero (same length as y)."""
    if len(y) < 2:
        return np.zeros_like(y)
    inc = (y[1:] + y[:-1]) * 0.5 * dt
    return np.concatenate([[0.0], np.cumsum(inc)])


def _fit_tail(x: np.ndarray, y: np.ndarray, fit_from: int):
    xs, ys = x[fit_from:], y[fit_from:]
    valid = np.isfinite(xs) & np.isfinite(ys)
    if valid.sum() < 2:
        return None
    slope, intercept = np.polyfit(xs[valid], ys[valid], 1)
    return float(slope), float(intercept)


def patlak(target_tac: List[float], input_tac: List[float],
           dt_min: float, fit_from: int = 0) -> Optional[dict]:
    Ct = np.asarray(target_tac, float)
    Cp = np.asarray(input_tac, float)
    if Ct.size != Cp.size or Ct.size < 3:
        return None
    cpd = np.where(Cp == 0, np.nan, Cp)
    x = _cumtrapz(Cp, dt_min) / cpd
    y = Ct / cpd
    fit = _fit_tail(x, y, fit_from)
    if fit is None:
        return None
    slope, intercept = fit
    return {"model": "Patlak", "slope": slope, "intercept": intercept,
            "param": "Ki", "x": x.tolist(), "y": y.tolist()}


def logan(target_tac: List[float], input_tac: List[float],
          dt_min: float, fit_from: int = 0) -> Optional[dict]:
    Ct = np.asarray(target_tac, float)
    Cp = np.asarray(input_tac, float)
    if Ct.size != Cp.size or Ct.size < 3:
        return None
    ctd = np.where(Ct == 0, np.nan, Ct)
    x = _cumtrapz(Cp, dt_min) / ctd
    y = _cumtrapz(Ct, dt_min) / ctd
    fit = _fit_tail(x, y, fit_from)
    if fit is None:
        return None
    slope, intercept = fit
    return {"model": "Logan", "slope": slope, "intercept": intercept,
            "param": "DVR", "x": x.tolist(), "y": y.tolist()}
