"""
roi_algorithms.py — All 18 segmentation operations + morph, wrapping SimpleITK.

Each static method follows the same contract:
  - Accepts an ROIVolume + parameters
  - Calls vol.push_undo() before any mutation
  - Writes the result back into vol.mask
  - Never raises; prints on error
"""

from __future__ import annotations

import threading
from collections import deque
from typing import TYPE_CHECKING

import numpy as np
import SimpleITK as sitk

if TYPE_CHECKING:
    from .roi_volume import ROIVolume


class ROIAlgorithms:

    # ── Internal helpers ──────────────────────────────────────────────────────

    @staticmethod
    def _img(vol: "ROIVolume") -> sitk.Image:
        i = sitk.GetImageFromArray(vol.arr)
        i.CopyInformation(vol.sitk_img)
        return sitk.Cast(i, sitk.sitkFloat32)

    @staticmethod
    def _label_mask(vol: "ROIVolume", label: int) -> sitk.Image:
        arr = (vol.mask == label).astype(np.uint8)
        m = sitk.GetImageFromArray(arr)
        m.CopyInformation(vol.sitk_img)
        return m

    @staticmethod
    def _writeback(vol: "ROIVolume", result: sitk.Image, label: int,
                   clear_first: bool = False) -> None:
        if clear_first:
            vol.mask[vol.mask == label] = 0
        arr = sitk.GetArrayFromImage(result).astype(bool)
        vol.mask[arr] = np.int16(label)

    # ── 1. Global Threshold ───────────────────────────────────────────────────

    @staticmethod
    def threshold_segment(vol: "ROIVolume", lo: float, hi: float, label: int,
                          axis: int = -1, slice_idx: int = -1) -> None:
        vol.push_undo()
        if axis >= 0 and slice_idx >= 0:
            img_sl = vol.get_image_slice(axis, slice_idx)
            msk_sl = vol.get_mask_slice(axis, slice_idx).copy()
            msk_sl[(img_sl >= lo) & (img_sl <= hi)] = np.int16(label)
            if   axis == 2: vol.mask[slice_idx, :, :] = msk_sl
            elif axis == 1: vol.mask[:, slice_idx, :] = msk_sl
            else:           vol.mask[:, :, slice_idx] = msk_sl
        else:
            vol.mask[(vol.arr >= lo) & (vol.arr <= hi)] = np.int16(label)

    # ── 2. Region Grow (BFS) ──────────────────────────────────────────────────

    @staticmethod
    def region_grow(vol: "ROIVolume", sx: int, sy: int, sz: int,
                    tolerance: float, label: int) -> None:
        img = vol.arr
        seed_val = float(img[sz, sy, sx])
        nz, ny, nx = img.shape
        visited = np.zeros(img.shape, bool)
        result  = np.zeros(img.shape, bool)
        q: deque = deque([(sz, sy, sx)])
        visited[sz, sy, sx] = True
        nbrs = [(-1,0,0),(1,0,0),(0,-1,0),(0,1,0),(0,0,-1),(0,0,1)]
        while q:
            z, y, x = q.popleft()
            if abs(float(img[z, y, x]) - seed_val) <= tolerance:
                result[z, y, x] = True
                for dz, dy, dx in nbrs:
                    nz_, ny_, nx_ = z+dz, y+dy, x+dx
                    if 0<=nz_<nz and 0<=ny_<ny and 0<=nx_<nx and not visited[nz_,ny_,nx_]:
                        visited[nz_,ny_,nx_] = True
                        q.append((nz_,ny_,nx_))
        vol.push_undo()
        vol.mask[result] = np.int16(label)

    # ── 3. Connected Threshold ────────────────────────────────────────────────

    @staticmethod
    def connected_threshold(vol: "ROIVolume", sx: int, sy: int, sz: int,
                             lo: float, hi: float, label: int) -> None:
        try:
            res = sitk.ConnectedThreshold(ROIAlgorithms._img(vol),
                                          seedList=[(sx, sy, sz)],
                                          lower=float(lo), upper=float(hi))
            vol.push_undo()
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[connected_threshold] {e}")

    # ── 4. Neighborhood Connected ─────────────────────────────────────────────

    @staticmethod
    def neighborhood_connected(vol: "ROIVolume", sx: int, sy: int, sz: int,
                                lo: float, hi: float, radius: int, label: int) -> None:
        try:
            res = sitk.NeighborhoodConnected(ROIAlgorithms._img(vol),
                                             seedList=[(sx, sy, sz)],
                                             lower=float(lo), upper=float(hi),
                                             radius=[radius]*3)
            vol.push_undo()
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[neighborhood_connected] {e}")

    # ── 5. Confidence Connected ───────────────────────────────────────────────

    @staticmethod
    def confidence_connected(vol: "ROIVolume", sx: int, sy: int, sz: int,
                              multiplier: float, iterations: int,
                              radius: int, label: int) -> None:
        try:
            res = sitk.ConfidenceConnected(ROIAlgorithms._img(vol),
                                           seedList=[(sx, sy, sz)],
                                           numberOfIterations=iterations,
                                           multiplier=float(multiplier),
                                           initialNeighborhoodRadius=radius)
            vol.push_undo()
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[confidence_connected] {e}")

    # ── 6. Flood Fill 2-D ─────────────────────────────────────────────────────

    @staticmethod
    def flood_fill_2d(vol: "ROIVolume", sx: int, sy: int, sz: int,
                      axis: int, tolerance: float, label: int) -> None:
        if   axis == 2: slc = vol.get_image_slice(2, sz);  seed = (sy, sx)
        elif axis == 1: slc = vol.get_image_slice(1, sy);  seed = (sz, sx)
        else:           slc = vol.get_image_slice(0, sx);  seed = (sz, sy)

        seed_val = float(slc[seed[0], seed[1]])
        h, w = slc.shape
        visited = np.zeros((h, w), bool)
        result  = np.zeros((h, w), bool)
        q: deque = deque([seed])
        visited[seed[0], seed[1]] = True
        while q:
            r, c = q.popleft()
            if abs(float(slc[r, c]) - seed_val) <= tolerance:
                result[r, c] = True
                for dr, dc in [(-1,0),(1,0),(0,-1),(0,1)]:
                    nr, nc = r+dr, c+dc
                    if 0<=nr<h and 0<=nc<w and not visited[nr, nc]:
                        visited[nr, nc] = True
                        q.append((nr, nc))
        vol.push_undo()
        if   axis == 2: vol.mask[sz,:,:][result] = np.int16(label)
        elif axis == 1: vol.mask[:,sy,:][result] = np.int16(label)
        else:           vol.mask[:,:,sx][result] = np.int16(label)

    # ── 7. Fast Marching ──────────────────────────────────────────────────────

    @staticmethod
    def fast_marching(vol: "ROIVolume", sx: int, sy: int, sz: int,
                      stopping_value: float, label: int) -> None:
        try:
            img = ROIAlgorithms._img(vol)
            speed = sitk.Cast(
                sitk.InvertIntensity(
                    sitk.GradientMagnitudeRecursiveGaussian(img, sigma=1.)),
                sitk.sitkFloat32)
            fm = sitk.FastMarchingImageFilter()
            fm.AddTrialPoint((sx, sy, sz))
            fm.SetStoppingValue(stopping_value)
            arrival = fm.Execute(speed)
            res = sitk.BinaryThreshold(arrival, 0, stopping_value, 1, 0)
            vol.push_undo()
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[fast_marching] {e}")

    # ── 8. Otsu Threshold ─────────────────────────────────────────────────────

    @staticmethod
    def otsu_threshold(vol: "ROIVolume", label: int,
                       bins: int = 128, classes: int = 1) -> None:
        try:
            img = ROIAlgorithms._img(vol)
            if classes <= 1:
                res = sitk.OtsuThreshold(img, 0, 1, bins)
            else:
                res = sitk.OtsuMultipleThresholds(img, classes, bins)
                res = sitk.BinaryThreshold(res, 1, classes, 1, 0)
            vol.push_undo()
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[otsu_threshold] {e}")

    # ── 9. K-Means ────────────────────────────────────────────────────────────

    @staticmethod
    def kmeans_cluster(vol: "ROIVolume", k: int, label: int) -> None:
        try:
            img = ROIAlgorithms._img(vol)
            init = [float(i) / k for i in range(k)]
            km = sitk.ScalarImageKmeans(img, init)
            res = sitk.BinaryThreshold(km, 1, k, 1, 0)
            vol.push_undo()
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[kmeans_cluster] {e}")

    # ── 10. Level Set Refine ──────────────────────────────────────────────────

    @staticmethod
    def level_set_refine(vol: "ROIVolume", label: int,
                         iterations: int, propagation: float,
                         curvature: float) -> None:
        try:
            img   = ROIAlgorithms._img(vol)
            lmask = ROIAlgorithms._label_mask(vol, label)
            sdf   = sitk.SignedMaurerDistanceMap(
                        lmask, insideIsPositive=True,
                        squaredDistance=False, useImageSpacing=True)
            sdf   = sitk.Cast(sdf, sitk.sitkFloat32)
            feat  = sitk.BoundedReciprocal(
                        sitk.GradientMagnitudeRecursiveGaussian(img, sigma=1.))
            ls = sitk.GeodesicActiveContourLevelSetImageFilter()
            ls.SetNumberOfIterations(iterations)
            ls.SetPropagationScaling(float(propagation))
            ls.SetCurvatureScaling(float(curvature))
            ls.SetAdvectionScaling(1.)
            result_sdf = ls.Execute(sdf, feat)
            res = sitk.BinaryThreshold(result_sdf, 0, 1e10, 1, 0)
            vol.push_undo()
            ROIAlgorithms._writeback(vol, res, label, clear_first=True)
        except Exception as e:
            print(f"[level_set_refine] {e}")

    # ── 11. Watershed ─────────────────────────────────────────────────────────

    @staticmethod
    def watershed(vol: "ROIVolume", label: int) -> None:
        try:
            img = ROIAlgorithms._img(vol)
            markers = sitk.OtsuThreshold(img, 0, 1)
            ws = sitk.MorphologicalWatershedFromMarkers(img, markers)
            res = sitk.BinaryThreshold(ws, 1, 100, 1, 0)
            vol.push_undo()
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[watershed] {e}")

    # ── 12. ROI Connected ─────────────────────────────────────────────────────

    @staticmethod
    def roi_connected(vol: "ROIVolume", sx: int, sy: int, sz: int,
                      src_label: int, dst_label: int) -> None:
        try:
            src = ROIAlgorithms._label_mask(vol, src_label)
            labeled = sitk.ConnectedComponent(src)
            arr = sitk.GetArrayFromImage(labeled)
            seed_val = int(arr[sz, sy, sx])
            if seed_val == 0:
                return
            res = sitk.BinaryThreshold(labeled, seed_val, seed_val, 1, 0)
            vol.push_undo()
            vol.mask[vol.mask == src_label] = 0
            ROIAlgorithms._writeback(vol, res, dst_label)
        except Exception as e:
            print(f"[roi_connected] {e}")

    # ── 13. Remove Small Components ───────────────────────────────────────────

    @staticmethod
    def remove_small_components(vol: "ROIVolume", label: int,
                                 min_size: int) -> None:
        try:
            m = ROIAlgorithms._label_mask(vol, label)
            labeled   = sitk.ConnectedComponent(m)
            relabeled = sitk.RelabelComponent(labeled, minimumObjectSize=min_size)
            res = sitk.BinaryThreshold(relabeled, 1, 100000, 1, 0)
            vol.push_undo()
            vol.mask[vol.mask == label] = 0
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[remove_small_components] {e}")

    # ── 14. Connected Components ──────────────────────────────────────────────

    @staticmethod
    def connected_components(vol: "ROIVolume", label: int,
                              max_comp: int) -> None:
        try:
            m         = ROIAlgorithms._label_mask(vol, label)
            labeled   = sitk.ConnectedComponent(m)
            relabeled = sitk.RelabelComponent(labeled)
            vol.push_undo()
            vol.mask[vol.mask == label] = 0
            arr = sitk.GetArrayFromImage(relabeled)
            for c in range(1, min(max_comp + 1, int(arr.max()) + 1)):
                new_lbl = min(label + c - 1, 255)
                vol.mask[arr == c] = np.int16(new_lbl)
        except Exception as e:
            print(f"[connected_components] {e}")

    # ── 15. Fill Holes ────────────────────────────────────────────────────────

    @staticmethod
    def fill_holes(vol: "ROIVolume", label: int, axis: int = -1) -> None:
        try:
            m = ROIAlgorithms._label_mask(vol, label)
            if axis == -1:
                res = sitk.BinaryFillhole(m)
            else:
                arr = sitk.GetArrayFromImage(m)
                out = arr.copy()
                if   axis == 2:
                    for i in range(arr.shape[0]):
                        sl = sitk.GetImageFromArray(arr[i])
                        out[i] = sitk.GetArrayFromImage(sitk.BinaryFillhole(sl))
                elif axis == 1:
                    for i in range(arr.shape[1]):
                        sl = sitk.GetImageFromArray(arr[:, i, :])
                        out[:, i, :] = sitk.GetArrayFromImage(sitk.BinaryFillhole(sl))
                else:
                    for i in range(arr.shape[2]):
                        sl = sitk.GetImageFromArray(arr[:, :, i])
                        out[:, :, i] = sitk.GetArrayFromImage(sitk.BinaryFillhole(sl))
                res = sitk.GetImageFromArray(out)
                res.CopyInformation(m)
            vol.push_undo()
            vol.mask[vol.mask == label] = 0
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[fill_holes] {e}")

    # ── 16. Make Shell ────────────────────────────────────────────────────────

    @staticmethod
    def make_shell(vol: "ROIVolume", label: int, thickness: int) -> None:
        try:
            m      = ROIAlgorithms._label_mask(vol, label)
            eroded = sitk.BinaryErode(m, [thickness]*3)
            shell  = sitk.And(m, sitk.Not(eroded))
            vol.push_undo()
            vol.mask[vol.mask == label] = 0
            ROIAlgorithms._writeback(vol, shell, label)
        except Exception as e:
            print(f"[make_shell] {e}")

    # ── 17. Low-Pass Smooth ───────────────────────────────────────────────────

    @staticmethod
    def low_pass_smooth(vol: "ROIVolume", label: int, sigma: float) -> None:
        try:
            m   = ROIAlgorithms._label_mask(vol, label)
            mf  = sitk.Cast(m, sitk.sitkFloat32)
            sm  = sitk.SmoothingRecursiveGaussian(mf, sigma=sigma)
            res = sitk.BinaryThreshold(sm, .5, 1e10, 1, 0)
            vol.push_undo()
            vol.mask[vol.mask == label] = 0
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[low_pass_smooth] {e}")

    # ── 18. Boolean Op ────────────────────────────────────────────────────────

    @staticmethod
    def boolean_op(vol: "ROIVolume", label_a: int, label_b: int,
                   op: str, dst_label: int) -> None:
        a = (vol.mask == label_a)
        b = (vol.mask == label_b)
        if   op == 'or':       result = a | b
        elif op == 'and':      result = a & b
        elif op == 'xor':      result = a ^ b
        elif op == 'not':      result = ~a
        elif op == 'subtract': result = a & ~b
        else:
            return
        vol.push_undo()
        vol.mask[vol.mask == label_a] = 0
        vol.mask[result] = np.int16(dst_label)

    # ── Morph Erode / Dilate ──────────────────────────────────────────────────

    @staticmethod
    def morph_erode_dilate(vol: "ROIVolume", label: int, radius: int) -> None:
        try:
            m = ROIAlgorithms._label_mask(vol, label)
            r = abs(radius)
            res = sitk.BinaryErode(m, [r]*3) if radius < 0 \
                  else sitk.BinaryDilate(m, [r]*3)
            vol.push_undo()
            vol.mask[vol.mask == label] = 0
            ROIAlgorithms._writeback(vol, res, label)
        except Exception as e:
            print(f"[morph_erode_dilate] {e}")
