"""
main_window.py — Application main window.

Multi-image model
-----------------
_volumes[0]  = reference (REF) — all ROI paint/segment operations use this.
_volumes[1+] = input images     — view-only; switching active is display-only.
_active_vol  = index of the image currently displayed in the viewer.
"""

from __future__ import annotations

import os
from typing import Optional

from PyQt6.QtCore import Qt, QThread, QObject, pyqtSignal
from PyQt6.QtGui import QAction, QKeySequence
from PyQt6.QtWidgets import (
    QApplication, QFileDialog, QMainWindow,
    QMessageBox, QSplitter, QStatusBar, QVBoxLayout, QWidget,
)

from ..core.roi_volume import ROIVolume
from .image_list_widget import ImageListWidget
from .ortho_viewer      import OrthoViewer
from .tool_panel        import ToolPanel


class _BgWorker(QObject):
    """Runs a callable on a worker thread and reports the result / error."""
    done = pyqtSignal(object)
    fail = pyqtSignal(str)

    def __init__(self, fn) -> None:
        super().__init__()
        self._fn = fn

    def run(self) -> None:
        try:
            self.done.emit(self._fn())
        except Exception as exc:          # noqa: BLE001 — report any failure
            self.fail.emit(str(exc))


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("ROISA — ROI Segmentation & Analysis")
        self.resize(1400, 860)

        # ── Multi-image / fusion state ────────────────────────────────────────
        # _volumes[0]  = reference (REF) — base layer + owns the ROI mask.
        # _volumes[1+] = input images   — composited as fusion overlays.
        # _active_vol  = layer currently *selected for editing* (fusion controls).
        # _vol_visible = whether each layer is composited into the view.
        self._volumes:    list[ROIVolume] = [ROIVolume()]
        self._vol_visible: list[bool]     = [True]
        self._vol_names:   list[str]      = ["(none)"]
        self._active_vol:  int            = 0

        # ── Widgets ───────────────────────────────────────────────────────────
        self._viewer = OrthoViewer(self)
        self._panel  = ToolPanel(self)
        self._image_list = ImageListWidget()

        # Right panel: image list on top, tool panel below
        panel_container = QWidget(self)
        panel_container.setMinimumWidth(310)
        panel_container.setMaximumWidth(360)
        pc_layout = QVBoxLayout(panel_container)
        pc_layout.setContentsMargins(0, 0, 0, 0)
        pc_layout.setSpacing(0)
        pc_layout.addWidget(self._image_list)
        pc_layout.addWidget(self._panel, 1)

        # ── Central splitter ──────────────────────────────────────────────────
        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.addWidget(self._viewer)
        splitter.addWidget(panel_container)
        splitter.setSizes([1050, 340])
        splitter.setHandleWidth(4)
        splitter.setStyleSheet("QSplitter::handle{background:#2a2a2a;}")
        self.setCentralWidget(splitter)

        # ── Seed image list ────────────────────────────────────────────────────
        self._image_list.add_image("(none)", is_ref=True)
        self._image_list.set_active(0)
        self._image_list.set_remove_enabled(0, False)

        # ── Wire ImageListWidget signals ───────────────────────────────────────
        self._image_list.activateRequested.connect(self._select_layer)
        self._image_list.visibilityToggled.connect(self._on_visibility_toggled)
        self._image_list.removeRequested.connect(self._remove_image)
        self._image_list.addRequested.connect(self._on_add_image)

        # ── Wire fusion controls (Navigation Viewer → Data Manager) ────────────
        self._panel.fusionColormapChanged.connect(self._on_fusion_colormap)
        self._panel.fusionAlphaChanged.connect(self._on_fusion_alpha)
        self._panel.fusionWindowChanged.connect(self._on_fusion_window)
        self._panel.baseVisibleToggled.connect(self._on_base_visible)

        # ── Wire Registration operator ─────────────────────────────────────────
        self._panel.registerRequested.connect(self._on_register)
        self._panel.manualTransformRequested.connect(self._on_manual_transform)
        self._panel.resetRegistrationRequested.connect(self._on_reset_registration)

        # ── Wire Quantification operator ───────────────────────────────────────
        self._panel.suvComputeRequested.connect(self._on_suv_compute)
        self._panel.suvAutofillRequested.connect(self._on_suv_autofill)
        self._panel.suvExportRequested.connect(self._on_suv_export)
        self._panel.tacComputeRequested.connect(self._on_tac_compute)
        self._panel.percentThresholdRequested.connect(self._on_percent_threshold)
        self._panel.roiRatioRequested.connect(self._on_roi_ratio)
        self._panel.roiHistRequested.connect(self._on_roi_hist)
        self._panel.interpolateRequested.connect(self._on_interpolate)
        self._panel.thresholdPreviewRequested.connect(self._on_threshold_preview)
        self._last_quant_rows = []

        # ── Wire viewer / panel ────────────────────────────────────────────────
        self._panel.setViewer(self._viewer)
        self._viewer.positionChanged.connect(self._panel.onPositionChanged)
        self._viewer.seedSet.connect(self._panel.onSeedSet)
        self._viewer.sliceReleased.connect(self._on_slice_released)

        for sv in (self._viewer._sag_view,
                   self._viewer._cor_view,
                   self._viewer._axi_view):
            sv.sliceDragged.connect(self._on_paint_drag)
            sv.sliceClicked.connect(self._on_paint_click)

        self._panel.refreshRequested.connect(self._on_refresh)

        # ── Status bar ────────────────────────────────────────────────────────
        self._sb = QStatusBar(self)
        self.setStatusBar(self._sb)
        self._sb.showMessage(
            "Ready — use File → Open to load a DICOM series or NIfTI.  "
            "Use ＋ Add in the Images panel to add input images.")

        self._build_menus()
        self._apply_stylesheet()
        self.setAcceptDrops(True)

        # Indeterminate progress indicator for background operations
        from PyQt6.QtWidgets import QProgressBar
        self._progress = QProgressBar(self)
        self._progress.setMaximumWidth(140)
        self._progress.setRange(0, 0)       # indeterminate
        self._progress.setVisible(False)
        self._progress.setTextVisible(False)
        self._sb.addPermanentWidget(self._progress)
        self._panel.busyChanged.connect(self._on_busy_changed)

    # ── Multi-image helpers ────────────────────────────────────────────────────

    def _ref_vol(self) -> ROIVolume:
        """The reference volume (index 0) — ROI operations always use this."""
        return self._volumes[0]

    def _active_vol(self) -> ROIVolume:
        """The volume currently displayed in the viewer."""
        idx = max(0, min(self._active_vol, len(self._volumes) - 1))
        return self._volumes[idx]

    def _install_ref_callback(self) -> None:
        """Hook the reference volume's change callback to refresh the viewer."""
        def _cb():
            self._viewer.refresh()
            self._panel.refreshStats()
        self._ref_vol().set_change_callback(_cb)

    def _sync_image_list(self) -> None:
        """Rebuild the ImageListWidget from the current _volumes state."""
        self._image_list.clear()
        for i, vol in enumerate(self._volumes):
            name = (os.path.basename(vol.first_dicom_file())
                    if vol.first_dicom_file()
                    else os.path.basename(self._vol_names[i])
                    if self._vol_names[i] != "(none)"
                    else ("REF" if i == 0 else f"Input {i}"))
            self._image_list.add_image(name, is_ref=(i == 0))
            self._image_list.set_enabled(i, self._vol_visible[i])
            # ✕ on REF is disabled while inputs exist
            if i == 0:
                self._image_list.set_remove_enabled(0, len(self._volumes) <= 1)
        self._image_list.set_active(self._active_vol)
        # Keep the Registration operator's moving-image dropdown in sync
        self._panel.setMovingImages(
            [(f"IN{i}  ({os.path.basename(self._vol_names[i])})"
              if self._vol_names[i] != "(none)" else f"IN{i}", i)
             for i in range(1, len(self._volumes))])

        # Keep the Quantification activity-image dropdown in sync (REF + inputs)
        quant_items = [("REF", 0)]
        quant_items += [(f"IN{i}", i) for i in range(1, len(self._volumes))]
        self._panel.setQuantImages(quant_items)

    # ── Fusion ──────────────────────────────────────────────────────────────────

    def _rebuild_fusion(self) -> None:
        """Recompose the viewer: REF base + each visible input as an overlay."""
        ref = self._ref_vol()
        if not ref.is_loaded():
            return
        # Base layer is always REF (owns geometry + mask)
        self._viewer.setVolume(ref)
        self._viewer.setBaseVisible(self._vol_visible[0])

        overlays = []
        for i in range(1, len(self._volumes)):
            if not self._vol_visible[i]:
                continue
            vol = self._volumes[i]
            arr = vol.resample_array_to(ref)
            if arr is None:
                continue
            overlays.append({
                "arr":      arr,
                "colormap": vol.colormap(),
                "alpha":    vol.fusion_alpha(),
                "wmin":     vol.vmin(),
                "wmax":     vol.vmax(),
            })
        self._viewer.setOverlays(overlays)
        self._viewer.refresh()

    def _push_fusion_target(self) -> None:
        """Load the selected layer's display params into the panel's controls."""
        idx = self._active_vol
        if idx < 0 or idx >= len(self._volumes):
            return
        vol = self._volumes[idx]
        is_base = (idx == 0)
        name = "REF" if is_base else f"IN{idx}"
        self._panel.setFusionTarget(
            name        = name,
            colormap    = vol.colormap() if not is_base else self._viewer.baseColormap(),
            alpha       = vol.fusion_alpha(),
            wmin        = vol.vmin(),
            wmax        = vol.vmax(),
            is_base     = is_base,
            base_visible= self._vol_visible[0])

    # ── Image list actions ─────────────────────────────────────────────────────

    def _select_layer(self, idx: int) -> None:
        """Select a layer for editing (does not change what is composited)."""
        if idx < 0 or idx >= len(self._volumes):
            return
        self._active_vol = idx
        self._image_list.set_active(idx)
        self._push_fusion_target()
        self._sb.showMessage(
            "Selected REF (base) — ROI operations apply here"
            if idx == 0
            else f"Selected IN{idx} — adjust its colormap / opacity / window")

    def _on_visibility_toggled(self, idx: int, on: bool) -> None:
        if idx < 0 or idx >= len(self._vol_visible):
            return
        self._vol_visible[idx] = on
        self._image_list.set_enabled(idx, on)
        self._rebuild_fusion()

    def _remove_image(self, idx: int) -> None:
        if self._busy():
            self._sb.showMessage("Busy — wait for the current operation to finish.")
            return
        if idx < 0 or idx >= len(self._volumes):
            return
        if idx == 0 and len(self._volumes) == 1:
            return  # can't remove the only image
        if idx == 0:
            self._sb.showMessage(
                "Cannot remove the reference image while input images exist. "
                "Open a new image to replace the reference.")
            return
        del self._volumes[idx]
        del self._vol_visible[idx]
        del self._vol_names[idx]
        if self._active_vol >= len(self._volumes):
            self._active_vol = len(self._volumes) - 1
        elif self._active_vol == idx:
            self._active_vol = 0
        self._sync_image_list()
        self._rebuild_fusion()
        self._push_fusion_target()

    def _on_add_image(self) -> None:
        """Load an additional input image as a fusion overlay."""
        if self._busy():
            self._sb.showMessage("Busy — wait for the current operation to finish.")
            return
        path = QFileDialog.getExistingDirectory(self, "Add Input DICOM Folder")
        if not path:
            path, _ = QFileDialog.getOpenFileName(
                self, "Add Input Image", "",
                "Images (*.nii *.nii.gz *.mha *.mhd *.nrrd);;All files (*)")
        if not path:
            return
        self._sb.showMessage(f"Loading input image: {path}…")
        QApplication.processEvents()
        vol = ROIVolume()
        if vol.load(path):
            # New overlays default to a 'hot' colormap so they stand out on REF
            vol.set_colormap(1)
            self._volumes.append(vol)
            self._vol_visible.append(True)
            self._vol_names.append(path)
            self._sync_image_list()
            self._rebuild_fusion()
            n = len(self._volumes) - 1
            self._sb.showMessage(
                f"Added IN{n}: {os.path.basename(path)} — fused over REF")
        else:
            self._sb.showMessage("Add failed — see console.")
            QMessageBox.warning(self, "Load error",
                                f"Could not load:\n{path}")

    # ── Fusion control handlers (from the Data Manager fusion group) ────────────

    def _on_fusion_colormap(self, cm: int) -> None:
        idx = self._active_vol
        if idx == 0:
            self._viewer.setBaseColormap(cm)   # REF base colormap
        elif 0 <= idx < len(self._volumes):
            self._volumes[idx].set_colormap(cm)
        self._rebuild_fusion()

    def _on_fusion_alpha(self, a: float) -> None:
        idx = self._active_vol
        if 0 <= idx < len(self._volumes):
            self._volumes[idx].set_fusion_alpha(a)
        self._rebuild_fusion()

    def _on_fusion_window(self, lo: float, hi: float) -> None:
        idx = self._active_vol
        if 0 <= idx < len(self._volumes):
            self._volumes[idx].set_window(lo, hi)
        self._rebuild_fusion()

    def _on_base_visible(self, on: bool) -> None:
        self._vol_visible[0] = on
        self._image_list.set_enabled(0, on)
        self._rebuild_fusion()

    # ── Background-task runner (keeps the UI responsive on large volumes) ────────

    def _run_bg(self, work, on_done, busy_msg: str) -> None:
        """Run `work()` on a thread; call `on_done(result)` on the GUI thread.

        Heavy operator controls are disabled for the duration so the same
        volume isn't mutated twice at once; the slice/3-D views stay live.
        """
        if getattr(self, "_bg_busy", False) or self._panel.is_seg_running():
            self._sb.showMessage("Busy — wait for the current operation to finish.")
            return
        self._bg_busy = True
        self._panel.setBusy(True)
        self._sb.showMessage(busy_msg)

        self._bg_thread = QThread(self)
        self._bg_worker = _BgWorker(work)
        self._bg_worker.moveToThread(self._bg_thread)
        self._bg_thread.started.connect(self._bg_worker.run)

        def finish() -> None:
            self._bg_busy = False
            self._panel.setBusy(False)
            self._bg_thread.quit()

        def ok(result) -> None:
            finish()
            on_done(result)

        def err(msg: str) -> None:
            finish()
            self._sb.showMessage(f"Operation failed: {msg}")

        self._bg_worker.done.connect(ok)
        self._bg_worker.fail.connect(err)
        self._bg_thread.finished.connect(self._bg_thread.deleteLater)
        self._bg_thread.start()

    # ── Registration handlers ───────────────────────────────────────────────────

    def _on_register(self, moving_idx: int, mode: str, iterations: int) -> None:
        from ..core.roi_volume import register_images
        ref = self._ref_vol()
        if not ref.is_loaded():
            self._panel.setRegStatus("Load a reference image first.")
            return
        if moving_idx < 1 or moving_idx >= len(self._volumes):
            self._panel.setRegStatus("Invalid moving image.")
            return
        mv = self._volumes[moving_idx]
        # Snapshot the (immutable) source images on the GUI thread; the worker
        # only reads them and returns a new image — the volume is mutated here.
        if mv._orig_sitk is None:
            mv._orig_sitk = mv._sitk_img
        moving_src, fixed_src = mv._orig_sitk, ref._sitk_img
        self._panel.setRegStatus(f"Registering IN{moving_idx} ({mode})… working in background")
        self._sb.showMessage(f"Registering IN{moving_idx} to REF ({mode})…")

        def work():
            return register_images(moving_src, fixed_src, mode, iterations)

        def done(result):
            if result is not None:
                mv._apply_resampled(result)
                mv._reg_manual = [0., 0., 0., 0., 0., 0.]
                self._panel.setRegStatus(
                    f"IN{moving_idx} registered to REF ({mode}). Now aligned in fusion.")
                self._sb.showMessage(f"Registration complete: IN{moving_idx} → REF ({mode})")
            else:
                self._panel.setRegStatus(f"Registration of IN{moving_idx} failed — see console.")
                self._sb.showMessage("Registration failed.")
            self._rebuild_fusion()

        self._run_bg(work, done, f"Registering IN{moving_idx} to REF ({mode})…")

    def _on_manual_transform(self, moving_idx: int, tx: float, ty: float,
                             tz: float, rx: float, ry: float, rz: float) -> None:
        ref = self._ref_vol()
        if not ref.is_loaded() or moving_idx < 1 or moving_idx >= len(self._volumes):
            return
        mv = self._volumes[moving_idx]
        QApplication.setOverrideCursor(Qt.CursorShape.WaitCursor)
        try:
            ok = mv.apply_manual_transform(ref, tx, ty, tz, rx, ry, rz)
        finally:
            QApplication.restoreOverrideCursor()
        self._panel.setRegStatus(
            f"Manual transform applied to IN{moving_idx}."
            if ok else "Manual transform failed.")
        self._rebuild_fusion()

    def _on_reset_registration(self, moving_idx: int) -> None:
        if moving_idx < 1 or moving_idx >= len(self._volumes):
            return
        mv = self._volumes[moving_idx]
        if mv.reset_registration():
            self._panel.setRegStatus(f"IN{moving_idx} restored to original (unregistered).")
        else:
            self._panel.setRegStatus(f"IN{moving_idx} has no registration to reset.")
        self._rebuild_fusion()

    # ── Quantification handlers ──────────────────────────────────────────────────

    def _activity_array(self, idx: int):
        """Return the activity image at `idx` resampled onto the REF grid."""
        ref = self._ref_vol()
        if idx <= 0 or idx >= len(self._volumes):
            return ref.arr
        return self._volumes[idx].resample_array_to(ref)

    def _on_suv_compute(self, activity_idx: int) -> None:
        from ..core.suv import suv_factor, roi_suv_stats
        import numpy as np
        ref = self._ref_vol()
        if not ref.is_loaded() or ref.mask is None:
            self._sb.showMessage("No activity image / ROI available.")
            return
        factor  = suv_factor(self._panel.suvParams())
        mask    = ref.mask
        spacing = ref.spacing_xyz()

        def work():
            act = self._activity_array(activity_idx)   # resample (heavy)
            if act is None:
                return []
            labels = [int(l) for l in np.unique(mask) if l != 0]
            rows = []
            for lbl in labels:
                st = roi_suv_stats(act, mask, lbl, spacing, factor)
                if st:
                    rows.append(st)
            return rows

        def done(rows):
            self._last_quant_rows = rows
            self._panel.setQuantResults(rows)
            self._sb.showMessage(
                f"SUV computed for {len(rows)} ROI(s) on "
                f"{'REF' if activity_idx == 0 else 'IN'+str(activity_idx)} "
                f"(factor {factor:.4e})")

        self._run_bg(work, done, "Computing SUV…")

    def _on_suv_autofill(self, activity_idx: int) -> None:
        from ..core.suv import extract_suv_params
        if activity_idx < 0 or activity_idx >= len(self._volumes):
            return
        vol = self._volumes[activity_idx]
        path = vol.first_dicom_file() or self._vol_names[activity_idx]
        p = extract_suv_params(path)
        if p is None:
            self._sb.showMessage(
                "Auto-fill failed — no DICOM metadata (pydicom). Enter values manually.")
            return
        self._panel.setSuvParams(p)
        self._sb.showMessage("SUV parameters auto-filled from DICOM.")

    def _on_suv_export(self) -> None:
        if not self._last_quant_rows:
            self._sb.showMessage("Nothing to export — Compute SUV first.")
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Export SUV CSV", "roisa_suv.csv", "CSV (*.csv)")
        if not path:
            return
        try:
            import csv
            with open(path, "w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["Label", "Voxels", "Volume_mL",
                            "SUVmean", "SUVmax", "SUVpeak", "TLG"])
                for d in self._last_quant_rows:
                    w.writerow([d["label"], d["voxels"], f"{d['volume_ml']:.4f}",
                                f"{d['suv_mean']:.4f}", f"{d['suv_max']:.4f}",
                                f"{d['suv_peak']:.4f}", f"{d['tlg']:.4f}"])
            self._sb.showMessage(f"Exported SUV table → {os.path.basename(path)}")
        except Exception as exc:
            QMessageBox.warning(self, "Export error", str(exc))

    def _on_tac_compute(self, label: int, activity_idx: int) -> None:
        from ..core.suv import suv_factor, time_activity_curve
        ref = self._ref_vol()
        if not ref.is_loaded() or ref.mask is None:
            return
        factor   = suv_factor(self._panel.suvParams())
        mask     = ref.mask
        n_inputs = len(self._volumes)

        def work():
            frames = [self._volumes[i].resample_array_to(ref)
                      for i in range(1, n_inputs)]
            if not frames:
                frames = [ref.arr]
            return time_activity_curve(frames, mask, label, factor)

        def done(tac):
            if not tac:
                self._sb.showMessage(f"Label {label} not present in the ROI mask.")
                self._panel.setTac([])
                return
            self._panel.setTac(tac, ylabel="SUVmean")
            self._sb.showMessage(
                f"TAC plotted for label {label} across {len(tac)} frame(s).")

        self._run_bg(work, done, "Computing time-activity curve…")

    # ── Analysis handlers ────────────────────────────────────────────────────────

    def _on_percent_threshold(self, source_label: int, pct: float,
                              target_label: int) -> None:
        from ..core.analysis import percent_threshold
        ref = self._ref_vol()
        if not ref.is_loaded() or ref.mask is None:
            self._sb.showMessage("Load a reference image first.")
            return
        act = self._activity_array(self._panel.activityIndex())
        if act is None:
            self._sb.showMessage("No activity image available.")
            return
        sel = percent_threshold(act, ref.mask, source_label, pct)
        if sel is None:
            self._sb.showMessage(
                "Percent threshold: source region empty or non-positive peak.")
            return
        ref.push_undo()
        ref.mask[sel] = target_label
        n = int(sel.sum())
        self._rebuild_fusion()
        self._panel.refreshStats()
        self._sb.showMessage(
            f"Percent threshold: {n} voxels ≥ {pct:.0f}% of peak → label {target_label}.")

    def _on_roi_ratio(self, label_a: int, label_b: int) -> None:
        from ..core.analysis import roi_ratio
        ref = self._ref_vol()
        if not ref.is_loaded() or ref.mask is None:
            return
        act = self._activity_array(self._panel.activityIndex())
        res = roi_ratio(act, ref.mask, label_a, label_b)
        if res is None:
            self._panel.setRoiRatioResult(
                f"Label {label_a} or {label_b} is empty.")
            return
        mean_a, mean_b, ratio = res
        self._panel.setRoiRatioResult(
            f"mean(L{label_a})={mean_a:.3g}   mean(L{label_b})={mean_b:.3g}\n"
            f"ratio A/B = {ratio:.3f}")
        self._sb.showMessage(f"ROI ratio L{label_a}/L{label_b} = {ratio:.3f}")

    def _on_roi_hist(self, label: int) -> None:
        from ..core.analysis import roi_histogram
        ref = self._ref_vol()
        if not ref.is_loaded() or ref.mask is None:
            return
        act = self._activity_array(self._panel.activityIndex())
        res = roi_histogram(act, ref.mask, label, bins=64)
        if res is None:
            self._panel.setRoiHist([], 0., 1., f"Label {label}: empty")
            self._sb.showMessage(f"Label {label} is empty.")
            return
        counts, vmin, vmax = res
        self._panel.setRoiHist(counts, vmin, vmax, f"Label {label} histogram")
        self._sb.showMessage(f"Histogram of label {label} ({int(counts.sum())} voxels).")

    # ── Segmentation/ROI tool handlers ──────────────────────────────────────────

    def _on_interpolate(self, label: int, axis: int) -> None:
        ref = self._ref_vol()
        if not ref.is_loaded():
            return
        ref.push_undo()
        filled = ref.interpolate_label(label, axis)
        self._rebuild_fusion()
        self._panel.refreshStats()
        self._sb.showMessage(
            f"Interpolated label {label}: filled {filled} slice(s)."
            if filled else
            f"Interpolate: need label {label} on ≥2 separated slices along that axis.")

    def _on_threshold_preview(self, lo: float, hi: float, on: bool) -> None:
        ref = self._ref_vol()
        if not on or not ref.is_loaded() or ref.arr is None:
            self._viewer.setPreviewVolume(None)
            self._viewer.refresh()
            return
        import numpy as np
        preview = (ref.arr >= lo) & (ref.arr <= hi)
        self._viewer.setPreviewVolume(preview)
        self._viewer.refresh()

    # ── Menu ──────────────────────────────────────────────────────────────────

    def _build_menus(self) -> None:
        mb = self.menuBar()

        fm = mb.addMenu("&File")
        open_act = QAction("&Open Reference (DICOM / NIfTI)…", self)
        open_act.setShortcut(QKeySequence("Ctrl+O"))
        open_act.triggered.connect(self._on_open)
        fm.addAction(open_act)

        fm.addSeparator()
        shot_act = QAction("Save Screenshot…", self)
        shot_act.setShortcut(QKeySequence("Ctrl+Shift+S"))
        shot_act.triggered.connect(self._on_screenshot)
        fm.addAction(shot_act)
        labels_act = QAction("Export Labels (NIfTI)…", self)
        labels_act.triggered.connect(self._on_export_labels)
        fm.addAction(labels_act)
        report_act = QAction("Generate Report (PDF/HTML)…", self)
        report_act.setShortcut(QKeySequence("Ctrl+R"))
        report_act.triggered.connect(self._on_generate_report)
        fm.addAction(report_act)

        fm.addSeparator()
        prefs_act = QAction("Preferences…", self)
        prefs_act.setShortcut(QKeySequence("Ctrl+,"))
        prefs_act.triggered.connect(self._on_settings)
        fm.addAction(prefs_act)

        fm.addSeparator()
        quit_act = QAction("Quit", self)
        quit_act.setShortcut(QKeySequence("Ctrl+Q"))
        quit_act.triggered.connect(QApplication.quit)
        fm.addAction(quit_act)

        vm = mb.addMenu("&View")
        layout_m = vm.addMenu("Layout Preset")
        for i, name in enumerate(["2×2", "1+3", "3-up", "Axial only", "3D only"]):
            act = QAction(name, self)
            act.setShortcut(QKeySequence(f"Ctrl+{i+1}"))
            act.triggered.connect(lambda _chk, idx=i: self._viewer.setLayoutPreset(idx))
            layout_m.addAction(act)

        hm = mb.addMenu("&Help")
        about = QAction("About ROISA", self)
        about.triggered.connect(lambda: QMessageBox.about(
            self, "ROISA",
            "<b>ROISA</b> — ROI Segmentation &amp; Analysis<br>"
            "Python/PyQt6 port<br><br>"
            "<b>Multi-image:</b> File → Open loads the <b>reference</b>. "
            "Use <b>＋ Add</b> in the Images panel to load input images. "
            "Click a row to view it; ● toggles it; ✕ removes it.<br>"
            "ROI paint &amp; segment always operate on the reference.<br><br>"
            "Stack: PyQt6 · SimpleITK · VTK · NumPy"))
        hm.addAction(about)

    # ── Handlers ──────────────────────────────────────────────────────────────

    def _busy(self) -> bool:
        """True while a heavy background op or segmentation is running."""
        return getattr(self, "_bg_busy", False) or self._panel.is_seg_running()

    def _open_path(self, path: str) -> None:
        """Load `path` as the new reference image — replaces all volumes."""
        if not path:
            return
        if self._busy():
            self._sb.showMessage("Busy — wait for the current operation to finish.")
            return
        self._sb.showMessage(f"Loading {path}…")
        QApplication.processEvents()
        vol = ROIVolume()
        if not vol.load(path):
            self._sb.showMessage("Load failed — see console.")
            QMessageBox.warning(self, "Load error", f"Could not load:\n{path}")
            return
        self._volumes    = [vol]
        self._vol_visible = [True]
        self._vol_names   = [path]
        self._active_vol  = 0
        self._panel.setVolume(vol)
        self._viewer.setVolume(vol)
        self._viewer.setBaseVisible(True)
        self._viewer.setOverlays([])
        self._install_ref_callback()
        self._sync_image_list()
        self._push_fusion_target()
        self._sb.showMessage(
            f"Loaded REF: {os.path.basename(path)}  —  "
            f"{vol.nx()}×{vol.ny()}×{vol.nz()} voxels, "
            f"spacing {vol.spacing_xyz()}")

    def _on_open(self) -> None:
        """Load a new reference image — replaces all existing volumes."""
        path = QFileDialog.getExistingDirectory(self, "Select DICOM folder")
        if not path:
            path, _ = QFileDialog.getOpenFileName(
                self, "Open NIfTI / image",
                "", "Images (*.nii *.nii.gz *.mha *.mhd *.nrrd);;All files (*)")
        self._open_path(path)

    # ── Drag-and-drop loading ───────────────────────────────────────────────────

    def dragEnterEvent(self, e) -> None:
        if e.mimeData().hasUrls():
            e.acceptProposedAction()

    def dropEvent(self, e) -> None:
        for url in e.mimeData().urls():
            p = url.toLocalFile()
            if p:
                self._open_path(p)
                break

    # ── Background progress indicator ───────────────────────────────────────────

    def _on_busy_changed(self, busy: bool) -> None:
        if hasattr(self, "_progress"):
            self._progress.setVisible(busy)

    # ── Keyboard shortcuts ──────────────────────────────────────────────────────

    def keyPressEvent(self, e) -> None:
        k = e.key()
        if Qt.Key.Key_1 <= k <= Qt.Key.Key_9:
            self._panel.setActiveLabel(k - Qt.Key.Key_0)
            self._sb.showMessage(f"Active label: {k - Qt.Key.Key_0}")
        elif k == Qt.Key.Key_P:
            self._panel.setToolByName("paint");   self._sb.showMessage("Paint")
        elif k == Qt.Key.Key_E:
            self._panel.setToolByName("erase");   self._sb.showMessage("Erase")
        elif k == Qt.Key.Key_S:
            self._panel.setToolByName("segment"); self._sb.showMessage("Segment")
        elif k == Qt.Key.Key_BracketLeft:
            self._panel.bumpBrush(-1)
        elif k == Qt.Key.Key_BracketRight:
            self._panel.bumpBrush(+1)
        elif k == Qt.Key.Key_Z:
            if self._ref_vol().is_loaded():
                self._ref_vol().undo()
                self._rebuild_fusion()
                self._panel.refreshStats()
        else:
            super().keyPressEvent(e)

    # ── Batch export of all labels ──────────────────────────────────────────────

    def _on_export_labels(self) -> None:
        ref = self._ref_vol()
        if not ref.is_loaded() or ref.mask is None:
            self._sb.showMessage("No mask to export.")
            return
        import numpy as np
        labels = [int(l) for l in np.unique(ref.mask) if l != 0]
        if not labels:
            self._sb.showMessage("Mask is empty — nothing to export.")
            return
        out_dir = QFileDialog.getExistingDirectory(self, "Export labels to folder")
        if not out_dir:
            return
        import SimpleITK as sitk
        written = 0
        for lbl in labels:
            binary = (ref.mask == lbl).astype(np.uint8)
            img = sitk.GetImageFromArray(binary)
            img.CopyInformation(ref.sitk_img)
            try:
                sitk.WriteImage(img, os.path.join(out_dir, f"label_{lbl}.nii.gz"))
                written += 1
            except Exception as exc:
                print(f"[export_labels] {exc}")
        self._sb.showMessage(f"Exported {written} label mask(s) → {out_dir}")

    # ── Settings / preferences ──────────────────────────────────────────────────

    def _on_settings(self) -> None:
        from .settings_dialog import SettingsDialog
        dlg = SettingsDialog(self)
        if dlg.exec():
            prefs = dlg.values()
            self._panel.applyPreferences(prefs)
            self._sb.showMessage("Preferences applied.")

    # ── Report ──────────────────────────────────────────────────────────────────

    def _pixmap_b64(self, pixmap) -> str:
        from PyQt6.QtCore import QByteArray, QBuffer, QIODevice
        ba = QByteArray()
        buf = QBuffer(ba)
        buf.open(QIODevice.OpenModeFlag.WriteOnly)
        pixmap.save(buf, "PNG")
        return bytes(ba.toBase64()).decode("ascii")

    def _on_generate_report(self) -> None:
        from ..core.report import build_report_html
        ref = self._ref_vol()
        if not ref.is_loaded():
            self._sb.showMessage("Load an image first.")
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Save report", "roisa_report.pdf",
            "PDF (*.pdf);;HTML (*.html)")
        if not path:
            return

        info = {
            "Name":     os.path.basename(self._vol_names[0]),
            "Dimensions": f"{ref.nx()} × {ref.ny()} × {ref.nz()} voxels",
            "Spacing":  f"{tuple(round(s,3) for s in ref.spacing_xyz())} mm",
            "Window":   f"{ref.vmin():.1f} … {ref.vmax():.1f}",
        }
        shots = [("Sagittal", self._viewer.grabSagittal()),
                 ("Coronal",  self._viewer.grabCoronal()),
                 ("Axial",    self._viewer.grabAxial()),
                 ("3-D",      self._viewer.grabVtk())]
        stats = [[s.label, s.voxel_count, f"{s.volume_mm3:.1f}",
                  f"{s.mean_intensity:.2f}", f"{s.std_intensity:.2f}"]
                 for s in ref.compute_all_stats()]
        suv = [[d["label"], f"{d['volume_ml']:.3f}", f"{d['suv_mean']:.2f}",
                f"{d['suv_max']:.2f}", f"{d['suv_peak']:.2f}", f"{d['tlg']:.2f}"]
               for d in getattr(self, "_last_quant_rows", [])]
        meas = self._viewer.measurements()

        if path.lower().endswith(".pdf"):
            self._render_report_pdf(path, info, shots, stats, suv, meas)
        else:
            imgs = [(cap, self._pixmap_b64(px)) for cap, px in shots]
            html = build_report_html("ROISA Report", info, imgs, stats, suv, meas)
            with open(path, "w", encoding="utf-8") as f:
                f.write(html)
        self._sb.showMessage(f"Report saved: {os.path.basename(path)}")

    def _render_report_pdf(self, path, info, shots, stats, suv, meas) -> None:
        from ..core.report import build_report_html
        from PyQt6.QtGui import QTextDocument
        from PyQt6.QtCore import QUrl
        from PyQt6.QtPrintSupport import QPrinter
        doc = QTextDocument()
        # Register snapshots as resources referenced by name in the HTML
        imgs = []
        for i, (cap, px) in enumerate(shots):
            name = f"shot{i}"
            doc.addResource(QTextDocument.ResourceType.ImageResource,
                            QUrl(name), px.toImage())
            imgs.append((cap, name))
        html = build_report_html("ROISA Report", info, imgs, stats, suv, meas,
                                  embed=False)
        doc.setHtml(html)
        printer = QPrinter(QPrinter.PrinterMode.HighResolution)
        printer.setOutputFormat(QPrinter.OutputFormat.PdfFormat)
        printer.setOutputFileName(path)
        doc.print(printer)

    def _on_refresh(self) -> None:
        self._viewer.refresh()
        self._panel.refreshStats()

    def _on_screenshot(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, "Save screenshot", "roisa_screenshot.png",
            "PNG (*.png);;JPEG (*.jpg);;All files (*)")
        if path:
            self.grab().save(path)
            self._sb.showMessage(f"Screenshot saved: {os.path.basename(path)}")

    def _on_slice_released(self) -> None:
        self._panel.refreshStats()

    def _on_paint_click(self, x: int, y: int, z: int) -> None:
        mode = self._panel.toolMode()
        if mode in ("paint", "erase"):
            self._ref_vol().push_undo()
            self._do_paint(x, y, z, mode)
            self._viewer.refresh()

    def _on_paint_drag(self, x: int, y: int, z: int) -> None:
        mode = self._panel.toolMode()
        if mode in ("paint", "erase"):
            self._do_paint(x, y, z, mode)
            self._viewer.refresh()

    def _do_paint(self, x: int, y: int, z: int, mode: str) -> None:
        """Always paints on the reference volume regardless of active view."""
        label = self._panel.activeLabel() if mode == "paint" else 0
        # Smart brush only gates painting (not erasing)
        smart = self._panel.smartBrush() and mode == "paint"
        self._ref_vol().paint_brush(
            cx=x, cy=y, cz=z,
            radius=self._panel.brushRadius(),
            shape=self._panel.brushShape(),
            two_d=self._panel.twoDOnly(),
            view_axis=self._viewer.activeAxis(),
            label=label,
            smart=smart,
            tol=self._panel.brushTolerance())

    # ── Stylesheet ────────────────────────────────────────────────────────────

    def _apply_stylesheet(self) -> None:
        self.setStyleSheet("""
            QMainWindow, QWidget { background:#1a1a1a; color:#ccc; }
            QMenuBar { background:#111; color:#ccc; }
            QMenuBar::item:selected { background:#333; }
            QMenu { background:#1e1e1e; color:#ccc; border:1px solid #333; }
            QMenu::item:selected { background:#2a4a6a; }
            QGroupBox { border:1px solid #333; border-radius:4px;
                        margin-top:8px; padding-top:8px; color:#aaa; }
            QGroupBox::title { subcontrol-origin:margin; left:8px; color:#9a9; }
            QLabel { color:#bbb; }
            QSpinBox, QDoubleSpinBox, QComboBox, QLineEdit {
                background:#252525; color:#ccc; border:1px solid #444;
                border-radius:2px; padding:2px 4px; }
            QPushButton { background:#2a2a2a; color:#bbb;
                          border:1px solid #444; border-radius:3px; padding:3px 8px; }
            QPushButton:hover { background:#333; }
            QPushButton:pressed { background:#1a3d5c; }
            QTabWidget::pane { border:1px solid #333; }
            QTabBar::tab { background:#222; color:#aaa; padding:4px 10px;
                           border:1px solid #333; border-bottom:none; }
            QTabBar::tab:selected { background:#2a2a2a; color:#eee; }
            QSlider::groove:horizontal { background:#333; height:4px; border-radius:2px; }
            QSlider::handle:horizontal { background:#668; width:12px; height:12px;
                                          margin:-4px 0; border-radius:6px; }
            QScrollArea { border:none; }
            QTableWidget { background:#1e1e1e; color:#ccc; gridline-color:#333; }
            QHeaderView::section { background:#252525; color:#aaa; border:1px solid #333; }
        """)
