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

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QAction, QKeySequence
from PyQt6.QtWidgets import (
    QApplication, QFileDialog, QMainWindow,
    QMessageBox, QSplitter, QStatusBar, QVBoxLayout, QWidget,
)

from ..core.roi_volume import ROIVolume
from .image_list_widget import ImageListWidget
from .ortho_viewer      import OrthoViewer
from .tool_panel        import ToolPanel


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

    def _on_open(self) -> None:
        """Load a new reference image — replaces all existing volumes."""
        path = QFileDialog.getExistingDirectory(self, "Select DICOM folder")
        if not path:
            path, _ = QFileDialog.getOpenFileName(
                self, "Open NIfTI / image",
                "", "Images (*.nii *.nii.gz *.mha *.mhd *.nrrd);;All files (*)")
        if not path:
            return
        self._sb.showMessage(f"Loading {path}…")
        QApplication.processEvents()

        vol = ROIVolume()
        if not vol.load(path):
            self._sb.showMessage("Load failed — see console.")
            QMessageBox.warning(self, "Load error", f"Could not load:\n{path}")
            return

        # Replace everything — this becomes the new reference
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
        self._ref_vol().paint_brush(
            cx=x, cy=y, cz=z,
            radius=self._panel.brushRadius(),
            shape=self._panel.brushShape(),
            two_d=self._panel.twoDOnly(),
            view_axis=self._viewer.activeAxis(),
            label=label)

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
