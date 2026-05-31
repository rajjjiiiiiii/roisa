"""
main_window.py — Application main window.
"""

from __future__ import annotations

import os
from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QAction, QKeySequence
from PyQt6.QtWidgets import (
    QApplication, QFileDialog, QMainWindow,
    QMessageBox, QSplitter, QStatusBar, QWidget,
)

from ..core.roi_volume import ROIVolume
from .ortho_viewer import OrthoViewer
from .tool_panel   import ToolPanel


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("ROISA — ROI Segmentation & Analysis")
        self.resize(1400, 860)
        self._vol    = ROIVolume()
        self._viewer = OrthoViewer(self)
        self._panel  = ToolPanel(self)

        # ── Central splitter ──────────────────────────────────────────────────
        splitter = QSplitter(Qt.Orientation.Horizontal, self)
        splitter.addWidget(self._viewer)
        splitter.addWidget(self._panel)
        splitter.setSizes([1050, 340])
        splitter.setHandleWidth(4)
        splitter.setStyleSheet("QSplitter::handle{background:#2a2a2a;}")
        self.setCentralWidget(splitter)

        # ── Wire up ───────────────────────────────────────────────────────────
        self._panel.setViewer(self._viewer)
        self._viewer.positionChanged.connect(self._panel.onPositionChanged)
        self._viewer.seedSet.connect(self._panel.onSeedSet)

        self._viewer.sliceReleased.connect(self._on_slice_released)

        # Painting: drag on any slice view → paint/erase via tool mode
        for sv in (self._viewer._sag_view,
                   self._viewer._cor_view,
                   self._viewer._axi_view):
            sv.sliceDragged.connect(self._on_paint_drag)
            sv.sliceClicked.connect(self._on_paint_click)

        self._panel.refreshRequested.connect(self._on_refresh)

        # ── Status bar ────────────────────────────────────────────────────────
        self._sb = QStatusBar(self)
        self.setStatusBar(self._sb)
        self._sb.showMessage("Ready — use File → Open to load a DICOM series or NIfTI.")

        self._build_menus()

        # Dark theme
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

    # ── Menu ──────────────────────────────────────────────────────────────────

    def _build_menus(self) -> None:
        mb = self.menuBar()

        # File
        fm = mb.addMenu("&File")
        open_act = QAction("&Open DICOM / NIfTI…", self)
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

        # View → Layout presets
        vm = mb.addMenu("&View")
        layout_m = vm.addMenu("Layout Preset")
        for i, name in enumerate(["2×2", "1+3", "3-up", "Axial only", "3D only"]):
            act = QAction(name, self)
            act.setShortcut(QKeySequence(f"Ctrl+{i+1}"))
            act.triggered.connect(lambda _chk, idx=i: self._viewer.setLayoutPreset(idx))
            layout_m.addAction(act)

        # Help
        hm = mb.addMenu("&Help")
        about = QAction("About ROISA", self)
        about.triggered.connect(lambda: QMessageBox.about(
            self, "ROISA",
            "<b>ROISA</b> — ROI Segmentation &amp; Analysis<br>"
            "Python/PyQt6 port<br><br>"
            "Stack: PyQt6 · SimpleITK · VTK · NumPy"))
        hm.addAction(about)

    # ── Handlers ──────────────────────────────────────────────────────────────

    def _on_open(self) -> None:
        path = QFileDialog.getExistingDirectory(
            self, "Select DICOM folder")
        if not path:
            path, _ = QFileDialog.getOpenFileName(
                self, "Open NIfTI / image",
                "", "Images (*.nii *.nii.gz *.mha *.mhd *.nrrd);;All files (*)")
        if not path:
            return
        self._sb.showMessage(f"Loading {path}…")
        QApplication.processEvents()
        if self._vol.load(path):
            self._panel.setVolume(self._vol)
            self._viewer.setVolume(self._vol)
            self._sb.showMessage(
                f"Loaded {os.path.basename(path)}  —  "
                f"{self._vol.nx()}×{self._vol.ny()}×{self._vol.nz()} voxels, "
                f"spacing {self._vol.spacing_xyz()}")
        else:
            self._sb.showMessage("Load failed — see console.")
            QMessageBox.warning(self, "Load error",
                                f"Could not load:\n{path}")

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
            self._vol.push_undo()
            self._do_paint(x, y, z, mode)
            self._viewer.refresh()

    def _on_paint_drag(self, x: int, y: int, z: int) -> None:
        mode = self._panel.toolMode()
        if mode in ("paint", "erase"):
            self._do_paint(x, y, z, mode)
            self._viewer.refresh()

    def _do_paint(self, x: int, y: int, z: int, mode: str) -> None:
        label = self._panel.activeLabel() if mode == "paint" else 0
        self._vol.paint_brush(
            cx=x, cy=y, cz=z,
            radius=self._panel.brushRadius(),
            shape=self._panel.brushShape(),
            two_d=self._panel.twoDOnly(),
            view_axis=self._viewer.activeAxis(),
            label=label)
