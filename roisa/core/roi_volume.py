"""
roi_volume.py — Image + mask data model (Python equivalent of C++ ROIVolume).

Storage convention
------------------
  numpy arrays are shaped  (nz, ny, nx)  — Z is axis-0, matching SimpleITK's
  GetArrayFromImage().  Spacing is kept in (sx, sy, sz) order as returned by
  SimpleITK's GetSpacing().

  get_image_slice(axis, idx) / get_mask_slice(axis, idx) both return (rows, cols):
    axis 2 (axial)    → arr[idx, :, :]   rows=ny  cols=nx
    axis 1 (coronal)  → arr[:, idx, :]   rows=nz  cols=nx
    axis 0 (sagittal) → arr[:, :, idx]   rows=nz  cols=ny
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

import numpy as np
import SimpleITK as sitk


# ── Label colour table (matches the C++ LABEL_COLORS in SliceView) ─────────────
LABEL_COLORS: List[Optional[Tuple[int, int, int]]] = [
    None,            # 0  background
    (255,  80,  80), # 1  red
    ( 80, 200,  80), # 2  green
    ( 80, 130, 255), # 3  blue
    (255, 200,  50), # 4  yellow
    (200,  80, 200), # 5  magenta
    ( 80, 220, 220), # 6  cyan
    (255, 160,  50), # 7  orange
    (160, 255,  80), # 8  lime
    (255, 100, 160), # 9  pink
    (100, 160, 255), # 10 light-blue
]


@dataclass
class LabelStats:
    label: int
    voxel_count: int
    volume_mm3: float
    mean_intensity: float
    std_intensity: float


class ROIVolume:
    """Single source-of-truth for one loaded image + its label mask."""

    MAX_LABELS = 256
    MAX_UNDO   = 20

    def __init__(self) -> None:
        self._sitk_img: Optional[sitk.Image] = None
        self._arr:      Optional[np.ndarray] = None   # float32 (nz,ny,nx)
        self._mask:     Optional[np.ndarray] = None   # int16   (nz,ny,nx)
        self._spacing:  Tuple[float,float,float] = (1., 1., 1.)  # (sx,sy,sz) mm
        self._origin:   tuple = (0., 0., 0.)
        self._wmin: float = 0.
        self._wmax: float = 1.
        self._true_min: float = 0.
        self._true_max: float = 1.
        self._undo_stack: List[np.ndarray] = []
        self._label_vis:  List[bool] = [True] * self.MAX_LABELS
        self._label_vis[0] = False
        self._loaded: bool = False
        self._first_dicom_file: str = ""
        self._on_change = None   # optional callable fired after mask writes

        # ── Fusion display state (per-layer) ──────────────────────────────────
        self._colormap:     int   = 0      # 0=gray 1=hot 2=cool 3=viridis
        self._fusion_alpha: float = 0.6    # opacity when used as an overlay
        # Cache of this volume's intensity resampled onto another volume's grid,
        # keyed by id(ref_volume); invalidated whenever this image reloads.
        self._resample_cache: dict = {}

        # Registration: original (pre-registration) image kept for Reset, plus
        # the current manual transform params [tx,ty,tz,rx,ry,rz] (mm, degrees).
        self._orig_sitk = None
        self._reg_manual = [0., 0., 0., 0., 0., 0.]

    # ── Loading ────────────────────────────────────────────────────────────────

    def load(self, path: str) -> bool:
        try:
            p = Path(path)
            if p.is_dir():
                reader = sitk.ImageSeriesReader()
                fnames = reader.GetGDCMSeriesFileNames(str(p))
                if not fnames:
                    return False
                reader.SetFileNames(fnames)
                reader.MetaDataDictionaryArrayUpdateOn()
                reader.LoadPrivateTagsOn()
                self._sitk_img = reader.Execute()
                self._first_dicom_file = str(fnames[0]) if fnames else ""
            else:
                self._sitk_img = sitk.ReadImage(str(p))
                self._first_dicom_file = ""

            img_f = sitk.Cast(self._sitk_img, sitk.sitkFloat32)
            self._arr = sitk.GetArrayFromImage(img_f)           # (nz,ny,nx)
            self._spacing = tuple(self._sitk_img.GetSpacing())  # (sx,sy,sz)
            self._origin  = tuple(self._sitk_img.GetOrigin())

            flat = self._arr.ravel()
            self._true_min = float(flat.min())
            self._true_max = float(flat.max())
            self._wmin = float(np.percentile(flat, 2))
            self._wmax = float(np.percentile(flat, 98))

            self._mask = np.zeros(self._arr.shape, dtype=np.int16)
            self._undo_stack.clear()
            self._resample_cache.clear()
            self._loaded = True
            return True
        except Exception as exc:
            print(f"[ROIVolume.load] {exc}")
            return False

    def is_loaded(self) -> bool:
        return self._loaded

    def first_dicom_file(self) -> str:
        """Path of the first DICOM file from the last load (empty for NIfTI)."""
        return self._first_dicom_file

    def set_change_callback(self, cb) -> None:
        """Register a zero-argument callable fired after mask writes."""
        self._on_change = cb

    # ── Fusion display state ───────────────────────────────────────────────────

    def colormap(self) -> int:              return self._colormap
    def set_colormap(self, cm: int) -> None: self._colormap = int(cm)

    def fusion_alpha(self) -> float:               return self._fusion_alpha
    def set_fusion_alpha(self, a: float) -> None:  self._fusion_alpha = float(a)

    def resample_array_to(self, ref: "ROIVolume") -> Optional[np.ndarray]:
        """Return this volume's intensity resampled onto `ref`'s grid (nz,ny,nx).

        Used when compositing this image as a fusion overlay on top of the
        reference, so slice indices line up.  Result is cached per reference.
        """
        if not self._loaded or ref is None or not ref.is_loaded():
            return None
        # Fast path: identical geometry → no resample needed
        if (self._arr is not None and ref._arr is not None
                and self._arr.shape == ref._arr.shape
                and self._spacing == ref._spacing
                and self._origin  == ref._origin):
            return self._arr
        key = id(ref)
        cached = self._resample_cache.get(key)
        if cached is not None and cached[0] == ref._arr.shape:
            return cached[1]
        try:
            res = sitk.Resample(
                sitk.Cast(self._sitk_img, sitk.sitkFloat32),
                ref._sitk_img,                       # reference grid
                sitk.Transform(),                    # identity (already aligned)
                sitk.sitkLinear,
                0.0,
                sitk.sitkFloat32)
            arr = sitk.GetArrayFromImage(res)
            self._resample_cache[key] = (ref._arr.shape, arr)
            return arr
        except Exception as exc:
            print(f"[resample_array_to] {exc}")
            return None

    def _notify_change(self) -> None:
        if self._on_change is not None:
            try:
                self._on_change()
            except Exception:
                pass

    # ── Dimensions ────────────────────────────────────────────────────────────

    def nx(self) -> int: return int(self._arr.shape[2]) if self._loaded else 0
    def ny(self) -> int: return int(self._arr.shape[1]) if self._loaded else 0
    def nz(self) -> int: return int(self._arr.shape[0]) if self._loaded else 0

    def voxel_spacing_mm(self) -> float:
        return float(min(self._spacing)) if self._loaded else 1.

    def spacing_xyz(self) -> Tuple[float, float, float]:
        return self._spacing

    # ── Window / Level ────────────────────────────────────────────────────────

    def vmin(self) -> float: return self._wmin
    def vmax(self) -> float: return self._wmax

    def set_window(self, lo: float, hi: float) -> None:
        self._wmin, self._wmax = lo, hi

    def reset_window(self) -> None:
        self._wmin, self._wmax = self._true_min, self._true_max

    # ── Voxel access ──────────────────────────────────────────────────────────

    def get_intensity(self, x: int, y: int, z: int) -> float:
        if not self._loaded:
            return 0.
        try:
            return float(self._arr[z, y, x])
        except IndexError:
            return 0.

    def get_image_slice(self, axis: int, idx: int) -> np.ndarray:
        """Return 2-D float32 (rows, cols)."""
        if not self._loaded:
            return np.zeros((1, 1), np.float32)
        if   axis == 2: return self._arr[idx, :, :]
        elif axis == 1: return self._arr[:, idx, :]
        else:           return self._arr[:, :, idx]

    def get_mask_slice(self, axis: int, idx: int) -> np.ndarray:
        """Return 2-D int16 (rows, cols)."""
        if not self._loaded:
            return np.zeros((1, 1), np.int16)
        if   axis == 2: return self._mask[idx, :, :]
        elif axis == 1: return self._mask[:, idx, :]
        else:           return self._mask[:, :, idx]

    # ── Undo ──────────────────────────────────────────────────────────────────

    def push_undo(self) -> None:
        self._undo_stack.append(self._mask.copy())
        if len(self._undo_stack) > self.MAX_UNDO:
            self._undo_stack.pop(0)

    def undo(self) -> None:
        if self._undo_stack:
            self._mask = self._undo_stack.pop()

    # ── Painting ──────────────────────────────────────────────────────────────

    def paint_brush(self, cx: int, cy: int, cz: int,
                    radius: int, shape: int,
                    two_d: bool, view_axis: int,
                    label: int) -> None:
        """Write `label` into all mask voxels covered by the brush."""
        if not self._loaded:
            return
        nz, ny, nx = self._mask.shape
        r = radius
        zlo = cz if two_d else max(0, cz - r)
        zhi = cz + 1 if two_d else min(nz, cz + r + 1)
        ylo, yhi = max(0, cy - r), min(ny, cy + r + 1)
        xlo, xhi = max(0, cx - r), min(nx, cx + r + 1)

        zz, yy, xx = np.mgrid[zlo:zhi, ylo:yhi, xlo:xhi]
        dz = np.zeros_like(zz) if two_d else (zz - cz)
        dy, dx = yy - cy, xx - cx

        if shape == 0:    # sphere
            inside = dx*dx + dy*dy + dz*dz <= r*r
        elif shape == 1:  # cylinder along view axis
            if   view_axis == 2: inside = dx*dx + dy*dy <= r*r
            elif view_axis == 1: inside = dx*dx + dz*dz <= r*r
            else:                inside = dy*dy + dz*dz <= r*r
        else:             # cube
            inside = (np.abs(dx) <= r) & (np.abs(dy) <= r) & (np.abs(dz) <= r)

        self._mask[zz[inside], yy[inside], xx[inside]] = np.int16(label)
        self._notify_change()

    def clear_label(self, label: int) -> None:
        if not self._loaded:
            return
        if label == -1:
            self._mask[:] = 0
        else:
            self._mask[self._mask == label] = 0

    # ── Label visibility ──────────────────────────────────────────────────────

    def label_visible(self, label: int) -> bool:
        if 0 <= label < self.MAX_LABELS:
            return self._label_vis[label]
        return False

    def set_all_labels_visible(self, vis: bool) -> None:
        self._label_vis = [vis] * self.MAX_LABELS
        self._label_vis[0] = False

    # ── Geometry helpers ──────────────────────────────────────────────────────

    def label_centroid(self, label: int) -> np.ndarray:
        if not self._loaded:
            return np.zeros(3)
        zz, yy, xx = np.where(self._mask == label)
        if len(zz) == 0:
            return np.zeros(3)
        return np.array([float(xx.mean()), float(yy.mean()), float(zz.mean())])

    def propagate_label(self, label: int, axis: int,
                        slice_idx: int, direction: int) -> int:
        if not self._loaded:
            return 0
        if   axis == 0: src = self._mask[:, :, slice_idx]; lim = self.nx()
        elif axis == 1: src = self._mask[:, slice_idx, :]; lim = self.ny()
        else:           src = self._mask[slice_idx, :, :]; lim = self.nz()

        tidx = slice_idx + direction
        if not (0 <= tidx < lim):
            return 0

        mask_src = (src == label)
        if   axis == 0: self._mask[:, :, tidx][mask_src] = label
        elif axis == 1: self._mask[:, tidx, :][mask_src] = label
        else:           self._mask[tidx, :, :][mask_src] = label
        return int(mask_src.sum())

    # ── Statistics ────────────────────────────────────────────────────────────

    def compute_all_stats(self) -> List[LabelStats]:
        if not self._loaded:
            return []
        sx, sy, sz = self._spacing
        vox_vol = sx * sy * sz
        out = []
        for lbl in range(1, self.MAX_LABELS):
            idx = np.where(self._mask == lbl)
            n = len(idx[0])
            if n == 0:
                continue
            vals = self._arr[idx]
            out.append(LabelStats(
                label=lbl,
                voxel_count=n,
                volume_mm3=n * vox_vol,
                mean_intensity=float(vals.mean()),
                std_intensity=float(vals.std()),
            ))
        return out

    def export_stats_csv(self, path: str) -> bool:
        try:
            with open(path, 'w', newline='') as f:
                w = csv.writer(f)
                w.writerow(['Label', 'Voxels', 'Volume_mm3', 'Mean', 'Std'])
                for s in self.compute_all_stats():
                    w.writerow([s.label, s.voxel_count,
                                f"{s.volume_mm3:.1f}",
                                f"{s.mean_intensity:.1f}",
                                f"{s.std_intensity:.1f}"])
            return True
        except Exception as exc:
            print(f"[export_stats_csv] {exc}")
            return False

    # ── I/O ───────────────────────────────────────────────────────────────────

    def save_mask(self, path: str) -> bool:
        if not self._loaded:
            return False
        try:
            m = sitk.GetImageFromArray(self._mask.astype(np.int16))
            m.CopyInformation(self._sitk_img)
            sitk.WriteImage(m, path)
            return True
        except Exception as exc:
            print(f"[save_mask] {exc}")
            return False

    def load_mask(self, path: str) -> bool:
        if not self._loaded:
            return False
        try:
            m = sitk.ReadImage(path, sitk.sitkInt16)
            arr = sitk.GetArrayFromImage(m)
            if arr.shape != self._mask.shape:
                print(f"[load_mask] shape mismatch {arr.shape} vs {self._mask.shape}")
                return False
            self.push_undo()
            self._mask = arr.astype(np.int16)
            return True
        except Exception as exc:
            print(f"[load_mask] {exc}")
            return False

    def resample_to_isotropic(self, spacing: float = 0.) -> bool:
        if not self._loaded:
            return False
        try:
            sp = float(min(self._spacing)) if spacing <= 0. else spacing
            orig = self._sitk_img.GetSize()
            orig_sp = self._sitk_img.GetSpacing()
            new_size = [int(round(orig[i] * orig_sp[i] / sp)) for i in range(3)]
            rs = sitk.ResampleImageFilter()
            rs.SetOutputSpacing((sp, sp, sp))
            rs.SetSize(new_size)
            rs.SetOutputDirection(self._sitk_img.GetDirection())
            rs.SetOutputOrigin(self._sitk_img.GetOrigin())
            rs.SetInterpolator(sitk.sitkLinear)
            rs.SetDefaultPixelValue(float(self._true_min))
            self._sitk_img = rs.Execute(self._sitk_img)
            self._arr = sitk.GetArrayFromImage(
                sitk.Cast(self._sitk_img, sitk.sitkFloat32))
            self._spacing = (sp, sp, sp)
            self._mask = np.zeros(self._arr.shape, dtype=np.int16)
            return True
        except Exception as exc:
            print(f"[resample_to_isotropic] {exc}")
            return False

    def load_registered_image(self, moving_path: str) -> bool:
        if not self._loaded:
            return False
        try:
            moving = sitk.Cast(sitk.ReadImage(moving_path), sitk.sitkFloat32)
            fixed  = sitk.Cast(self._sitk_img, sitk.sitkFloat32)
            reg = sitk.ImageRegistrationMethod()
            reg.SetMetricAsMattesMutualInformation(50)
            reg.SetOptimizerAsGradientDescent(
                learningRate=1., numberOfIterations=100)
            reg.SetOptimizerScalesFromPhysicalShift()
            reg.SetShrinkFactorsPerLevel([4, 2, 1])
            reg.SetSmoothingSigmasPerLevel([2., 1., 0.])
            reg.SmoothingSigmasAreSpecifiedInPhysicalUnitsOn()
            reg.SetInterpolator(sitk.sitkLinear)
            tx = sitk.CenteredTransformInitializer(
                fixed, moving, sitk.Euler3DTransform(),
                sitk.CenteredTransformInitializerFilter.GEOMETRY)
            reg.SetInitialTransform(tx, inPlace=False)
            out_tx = reg.Execute(fixed, moving)
            resampled = sitk.Resample(moving, fixed, out_tx,
                                      sitk.sitkLinear, 0., moving.GetPixelID())
            self._sitk_img = resampled
            self._arr = sitk.GetArrayFromImage(
                sitk.Cast(resampled, sitk.sitkFloat32))
            flat = self._arr.ravel()
            self._true_min = float(flat.min())
            self._true_max = float(flat.max())
            self._wmin = float(np.percentile(flat, 2))
            self._wmax = float(np.percentile(flat, 98))
            return True
        except Exception as exc:
            print(f"[load_registered_image] {exc}")
            return False

    # ── Registration to a reference volume ─────────────────────────────────────

    def _apply_resampled(self, resampled: "sitk.Image") -> None:
        """Adopt a resampled image as this volume's data (keeps W/L, resets mask)."""
        self._sitk_img = resampled
        self._arr = sitk.GetArrayFromImage(sitk.Cast(resampled, sitk.sitkFloat32))
        self._spacing = tuple(resampled.GetSpacing())
        self._origin  = tuple(resampled.GetOrigin())
        self._mask = np.zeros(self._arr.shape, dtype=np.int16)
        self._undo_stack.clear()
        self._resample_cache.clear()

    def register_to(self, fixed: "ROIVolume", mode: str = "rigid",
                    iterations: int = 100) -> bool:
        """Register this (moving) image to `fixed` and resample into its frame.

        mode: 'rigid' (Euler3D) | 'affine' | 'deformable' (BSpline).
        The original image is preserved so reset_registration() can restore it.
        """
        if not self._loaded or fixed is None or not fixed.is_loaded():
            return False
        try:
            if self._orig_sitk is None:
                self._orig_sitk = self._sitk_img      # backup for reset
            moving = sitk.Cast(self._orig_sitk, sitk.sitkFloat32)
            fix    = sitk.Cast(fixed._sitk_img, sitk.sitkFloat32)

            reg = sitk.ImageRegistrationMethod()
            reg.SetMetricAsMattesMutualInformation(50)
            reg.SetMetricSamplingStrategy(reg.RANDOM)
            reg.SetMetricSamplingPercentage(0.10)
            reg.SetInterpolator(sitk.sitkLinear)
            reg.SetOptimizerScalesFromPhysicalShift()
            reg.SetShrinkFactorsPerLevel([4, 2, 1])
            reg.SetSmoothingSigmasPerLevel([2., 1., 0.])
            reg.SmoothingSigmasAreSpecifiedInPhysicalUnitsOn()

            if mode == "deformable":
                # Pre-align rigidly, then refine with a B-spline mesh
                rigid = sitk.CenteredTransformInitializer(
                    fix, moving, sitk.Euler3DTransform(),
                    sitk.CenteredTransformInitializerFilter.GEOMETRY)
                init = sitk.BSplineTransformInitializer(fix, [8, 8, 8])
                reg.SetMovingInitialTransform(rigid)
                reg.SetInitialTransform(init, inPlace=True)
                reg.SetOptimizerAsGradientDescentLineSearch(
                    learningRate=1., numberOfIterations=max(20, iterations // 2))
                bspline = reg.Execute(fix, moving)
                out_tx = sitk.CompositeTransform([rigid, bspline])
            else:
                base = (sitk.AffineTransform(3) if mode == "affine"
                        else sitk.Euler3DTransform())
                init = sitk.CenteredTransformInitializer(
                    fix, moving, base,
                    sitk.CenteredTransformInitializerFilter.GEOMETRY)
                reg.SetOptimizerAsGradientDescent(
                    learningRate=1., numberOfIterations=iterations)
                reg.SetInitialTransform(init, inPlace=False)
                out_tx = reg.Execute(fix, moving)

            resampled = sitk.Resample(moving, fix, out_tx,
                                      sitk.sitkLinear, 0., sitk.sitkFloat32)
            self._apply_resampled(resampled)
            self._reg_manual = [0., 0., 0., 0., 0., 0.]
            return True
        except Exception as exc:
            print(f"[register_to] {exc}")
            return False

    def apply_manual_transform(self, fixed: "ROIVolume",
                               tx: float, ty: float, tz: float,
                               rx: float, ry: float, rz: float) -> bool:
        """Apply a manual rigid transform (mm translation + degree rotation)
        to the original moving image and resample into `fixed`'s frame."""
        if not self._loaded or fixed is None or not fixed.is_loaded():
            return False
        try:
            import math
            if self._orig_sitk is None:
                self._orig_sitk = self._sitk_img
            moving = sitk.Cast(self._orig_sitk, sitk.sitkFloat32)
            fix    = sitk.Cast(fixed._sitk_img, sitk.sitkFloat32)
            e = sitk.Euler3DTransform()
            sz = moving.GetSize()
            center = moving.TransformContinuousIndexToPhysicalPoint(
                [(s - 1) / 2.0 for s in sz])
            e.SetCenter(center)
            e.SetRotation(math.radians(rx), math.radians(ry), math.radians(rz))
            e.SetTranslation((tx, ty, tz))
            resampled = sitk.Resample(moving, fix, e,
                                      sitk.sitkLinear, 0., sitk.sitkFloat32)
            self._apply_resampled(resampled)
            self._reg_manual = [tx, ty, tz, rx, ry, rz]
            return True
        except Exception as exc:
            print(f"[apply_manual_transform] {exc}")
            return False

    def reset_registration(self) -> bool:
        """Restore the original (pre-registration) image."""
        if self._orig_sitk is None:
            return False
        self._apply_resampled(self._orig_sitk)
        self._orig_sitk = None
        self._reg_manual = [0., 0., 0., 0., 0., 0.]
        return True

    def is_registered(self) -> bool:
        return self._orig_sitk is not None

    # ── Raw access (VTK, algorithms) ──────────────────────────────────────────

    @property
    def arr(self) -> Optional[np.ndarray]:   return self._arr
    @property
    def mask(self) -> Optional[np.ndarray]:  return self._mask
    @mask.setter
    def mask(self, v: np.ndarray) -> None:   self._mask = v.astype(np.int16)
    @property
    def sitk_img(self) -> Optional[sitk.Image]: return self._sitk_img

    def sitk_mask(self) -> Optional[sitk.Image]:
        if not self._loaded:
            return None
        m = sitk.GetImageFromArray(self._mask.astype(np.int16))
        m.CopyInformation(self._sitk_img)
        return m
