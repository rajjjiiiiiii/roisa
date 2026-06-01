"""
ortho_viewer.py — Four-panel orthogonal viewer with 5 switchable layout presets.

Preset 0  "2×2"   VTK | Sag / Cor | Axi  (equal quadrants, default)
Preset 1  "1+3"   large Axial | Sag / Cor / VTK stacked
Preset 2  "3-up"  Sag | Cor | Axi side-by-side (VTK hidden)
Preset 3  "Axi"   Axial only
Preset 4  "3D"    VTK only
"""

from __future__ import annotations

from typing import Optional

import numpy as np
from PyQt6.QtCore import QTimer, Qt, pyqtSignal
from PyQt6.QtGui import QPixmap
from PyQt6.QtWidgets import (
    QFrame, QHBoxLayout, QPushButton, QSplitter,
    QVBoxLayout, QWidget,
)

from .slice_view import SliceView
from .vtk_view   import VtkView


def _style_splitter(s: QSplitter) -> None:
    s.setHandleWidth(3)
    s.setStyleSheet("QSplitter::handle{background:#2a2a2a;}")


class OrthoViewer(QWidget):
    seedSet         = pyqtSignal(int, int, int)
    positionChanged = pyqtSignal(int, int, int)
    sliceReleased   = pyqtSignal()
    measurementAdded = pyqtSignal(str)
    polygonClosed   = pyqtSignal(int, object)

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._vol   = None
        self._x = self._y = self._z = 0
        self._sx = self._sy = self._sz = 0
        self._active_axis = 2
        self._current_preset = 0

        # Cine
        self._cine_timer = QTimer(self)
        self._cine_timer.timeout.connect(self._on_cine_tick)
        self._cine_axis    = 2
        self._cine_fps     = 8
        self._cine_playing = False

        # ── Create the four panels ────────────────────────────────────────────
        self._vtk_view = VtkView(self)
        self._sag_view = SliceView(0, self)
        self._cor_view = SliceView(1, self)
        self._axi_view = SliceView(2, self)

        # ── Outer layout ──────────────────────────────────────────────────────
        outer = QVBoxLayout(self)
        outer.setContentsMargins(2, 2, 2, 2)
        outer.setSpacing(2)

        # Preset toolbar
        toolbar = QWidget(self)
        toolbar.setFixedHeight(26)
        toolbar.setStyleSheet(
            "QWidget{background:#1a1a1a;}"
            "QPushButton{background:#252525;color:#aaa;border:1px solid #3a3a3a;"
            "  padding:1px 7px;font-size:11px;border-radius:2px;}"
            "QPushButton:checked{background:#1a3d5c;color:#7ec8ff;"
            "  border:1px solid #3080b0;}"
            "QPushButton:hover:!checked{background:#2e2e2e;}")
        tbL = QHBoxLayout(toolbar)
        tbL.setContentsMargins(3, 3, 3, 3)
        tbL.setSpacing(3)

        PRESETS = [
            ("2×2",  "Four equal panels (VTK | Sag / Cor | Axi)"),
            ("1+3",  "Large Axial + Sag / Cor / VTK stacked"),
            ("3-up", "Three slices side-by-side (Sag | Cor | Axi)"),
            ("Axi",  "Axial view only"),
            ("3D",   "3-D VTK view only"),
            ("1×4",  "All four panels in a row (Sag | Cor | Axi | 3D)"),
        ]
        self._preset_btns: list[QPushButton] = []
        for i, (lbl, tip) in enumerate(PRESETS):
            btn = QPushButton(lbl, toolbar)
            btn.setToolTip(tip)
            btn.setCheckable(True)
            btn.setFixedHeight(20)
            tbL.addWidget(btn)
            btn.clicked.connect(lambda _checked, idx=i: self._apply_preset(idx))
            self._preset_btns.append(btn)
        tbL.addStretch()
        outer.addWidget(toolbar)

        # View container
        self._container = QWidget(self)
        self._container_layout = QVBoxLayout(self._container)
        self._container_layout.setContentsMargins(0, 0, 0, 0)
        self._container_layout.setSpacing(0)
        outer.addWidget(self._container, 1)

        # ── Wire slice signals ────────────────────────────────────────────────
        for axis, sv in [(0, self._sag_view),
                         (1, self._cor_view),
                         (2, self._axi_view)]:
            sv.sliceClicked.connect(
                lambda x,y,z,a=axis: (setattr(self,'_active_axis',a),
                                      self._on_clicked(x,y,z)))
            sv.sliceDragged.connect(
                lambda x,y,z,a=axis: (setattr(self,'_active_axis',a),
                                      self._on_dragged(x,y,z)))
            sv.sliceReleased.connect(self.sliceReleased)
            sv.scrolled.connect(
                lambda d, a=axis: self._on_scrolled(a, d))
            sv.measurementAdded.connect(self.measurementAdded)
            sv.polygonClosed.connect(self.polygonClosed)

        self._current_splitter: Optional[QSplitter] = None
        self._apply_preset(0)

    # ── Layout presets ─────────────────────────────────────────────────────────

    def _apply_preset(self, preset: int) -> None:
        # Detach all views
        for w in (self._vtk_view, self._sag_view,
                  self._cor_view, self._axi_view):
            w.setParent(self._container)
            w.hide()

        if self._current_splitter:
            self._container_layout.removeWidget(self._current_splitter)
            self._current_splitter.deleteLater()
            self._current_splitter = None

        root: QSplitter

        if preset == 0:
            root  = QSplitter(Qt.Orientation.Horizontal)
            leftV = QSplitter(Qt.Orientation.Vertical)
            rghtV = QSplitter(Qt.Orientation.Vertical)
            leftV.addWidget(self._vtk_view); leftV.addWidget(self._cor_view)
            rghtV.addWidget(self._sag_view); rghtV.addWidget(self._axi_view)
            root.addWidget(leftV); root.addWidget(rghtV)
            _style_splitter(root); _style_splitter(leftV); _style_splitter(rghtV)
        elif preset == 1:
            root  = QSplitter(Qt.Orientation.Horizontal)
            rghtV = QSplitter(Qt.Orientation.Vertical)
            rghtV.addWidget(self._sag_view)
            rghtV.addWidget(self._cor_view)
            rghtV.addWidget(self._vtk_view)
            root.addWidget(self._axi_view)
            root.addWidget(rghtV)
            _style_splitter(root); _style_splitter(rghtV)
            QTimer.singleShot(0, lambda r=root: r.setSizes(
                [r.width()*7//10, r.width()*3//10]) if r.width() > 0 else None)
        elif preset == 2:
            root = QSplitter(Qt.Orientation.Horizontal)
            root.addWidget(self._sag_view)
            root.addWidget(self._cor_view)
            root.addWidget(self._axi_view)
            _style_splitter(root)
        elif preset == 3:
            root = QSplitter(Qt.Orientation.Horizontal)
            root.addWidget(self._axi_view)
            _style_splitter(root)
        elif preset == 4:
            root = QSplitter(Qt.Orientation.Horizontal)
            root.addWidget(self._vtk_view)
            _style_splitter(root)
        else:   # preset == 5 — 1×4 row: Sag | Cor | Axi | 3D
            root = QSplitter(Qt.Orientation.Horizontal)
            root.addWidget(self._sag_view)
            root.addWidget(self._cor_view)
            root.addWidget(self._axi_view)
            root.addWidget(self._vtk_view)
            _style_splitter(root)

        self._current_splitter = root
        self._container_layout.addWidget(root)

        for i, btn in enumerate(self._preset_btns):
            btn.setChecked(i == preset)
        self._current_preset = preset

    def setLayoutPreset(self, preset: int) -> None:
        self._apply_preset(preset)

    def layoutPreset(self) -> int:
        return self._current_preset

    # ── Volume assignment ──────────────────────────────────────────────────────

    def setVolume(self, vol) -> None:
        self._vol = vol
        self._sag_view.setVolume(vol)
        self._cor_view.setVolume(vol)
        self._axi_view.setVolume(vol)
        self._vtk_view.setVolume(vol)
        if vol and vol.is_loaded():
            self._x = vol.nx() // 2
            self._y = vol.ny() // 2
            self._z = vol.nz() // 2
            self._sag_view.setSliceIndex(self._x)
            self._cor_view.setSliceIndex(self._y)
            self._axi_view.setSliceIndex(self._z)
            self._update_crosshairs()

    # ── Navigation ─────────────────────────────────────────────────────────────

    def setX(self, x: int) -> None:
        self._x = x; self._sag_view.setSliceIndex(x); self._update_crosshairs()

    def setY(self, y: int) -> None:
        self._y = y; self._cor_view.setSliceIndex(y); self._update_crosshairs()

    def setZ(self, z: int) -> None:
        self._z = z; self._axi_view.setSliceIndex(z); self._update_crosshairs()

    def x(self) -> int: return self._x
    def y(self) -> int: return self._y
    def z(self) -> int: return self._z
    def seedX(self) -> int: return self._sx
    def seedY(self) -> int: return self._sy
    def seedZ(self) -> int: return self._sz
    def activeAxis(self) -> int: return self._active_axis

    def refresh(self) -> None:
        self._sag_view.update()
        self._cor_view.update()
        self._axi_view.update()

    # ── VTK controls ───────────────────────────────────────────────────────────

    def refreshSurface(self, label: int = -1) -> None:
        self._vtk_view.refreshSurface(label)

    def refreshVtk(self) -> None:
        self._vtk_view.refreshVolume()
        self._vtk_view.refreshSurface(-1)

    def setVtkRenderMode(self, mode: int) -> None:
        self._vtk_view.setRenderMode(mode)

    def setVtkClip(self, enabled: bool, axis: int, frac: float) -> None:
        self._vtk_view.setClip(enabled, axis, frac)

    def resetVtkCamera(self) -> None:
        self._vtk_view.resetCamera()

    def refreshHistogram(self) -> None: pass  # kept for API compat

    # ── Display options ────────────────────────────────────────────────────────

    def setInterpolate(self, on: bool) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setInterpolate(on)

    def setColormap(self, cm: int) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setColormap(cm)

    def setOverlays(self, overlays) -> None:
        """Push fusion overlay layers (on the REF grid) to all slice views."""
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setOverlays(overlays)

    def setBaseVisible(self, on: bool) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setBaseVisible(on)

    def setBaseColormap(self, cm: int) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setColormap(cm)

    def baseColormap(self) -> int:
        return self._axi_view.colormap()

    def setOverlayAlpha(self, a: float) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setOverlayAlpha(a)

    def setAllLabelsVisible(self, v: bool) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setAllLabelsVisible(v)

    def setShowInfoOverlay(self, on: bool) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setShowInfoOverlay(on)

    def setProjectionMode(self, mode: int) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setProjectionMode(mode)

    def setSlab(self, slab: int) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setSlab(slab)

    def setShowColorbar(self, on: bool) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setShowColorbar(on)

    def setPreviewVolume(self, vol) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setPreviewVolume(vol)

    def setPolygonMode(self, on: bool) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setPolygonMode(on)

    def setMeasureMode(self, mode: int) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.setMeasureMode(mode)

    def clearMeasurements(self) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.clearMeasurements()

    def measurements(self) -> list:
        out = []
        for name, sv in (("Sagittal", self._sag_view),
                         ("Coronal", self._cor_view),
                         ("Axial", self._axi_view)):
            for m in sv.measurements():
                out.append(f"{name}: {m}")
        return out

    def resetAllZoom(self) -> None:
        for sv in (self._sag_view, self._cor_view, self._axi_view):
            sv.resetZoom()

    # ── Cine player ────────────────────────────────────────────────────────────

    def playCine(self) -> None:
        if not self._vol or not self._vol.is_loaded():
            return
        self._cine_playing = True
        self._cine_timer.start(max(1, 1000 // self._cine_fps))

    def stopCine(self) -> None:
        self._cine_timer.stop()
        self._cine_playing = False

    def setCineAxis(self, axis: int) -> None:
        self._cine_axis = max(0, min(2, axis))

    def setCineFps(self, fps: int) -> None:
        self._cine_fps = max(1, min(30, fps))
        if self._cine_timer.isActive():
            self._cine_timer.setInterval(1000 // self._cine_fps)

    def cinePlaying(self) -> bool:
        return self._cine_playing

    def _on_cine_tick(self) -> None:
        if not self._vol or not self._vol.is_loaded():
            self.stopCine(); return
        if self._cine_axis == 0:
            self._x = (self._x + 1) % self._vol.nx()
            self._sag_view.setSliceIndex(self._x)
        elif self._cine_axis == 1:
            self._y = (self._y + 1) % self._vol.ny()
            self._cor_view.setSliceIndex(self._y)
        else:
            self._z = (self._z + 1) % self._vol.nz()
            self._axi_view.setSliceIndex(self._z)
        self._update_crosshairs()
        self.positionChanged.emit(self._x, self._y, self._z)

    # ── Panel snapshots ────────────────────────────────────────────────────────

    def grabSagittal(self) -> QPixmap: return self._sag_view.grab()
    def grabCoronal(self)  -> QPixmap: return self._cor_view.grab()
    def grabAxial(self)    -> QPixmap: return self._axi_view.grab()
    def grabVtk(self)      -> QPixmap: return self._vtk_view.grab()

    # ── Internal handlers ──────────────────────────────────────────────────────

    def _update_crosshairs(self) -> None:
        self._sag_view.setCrosshair(self._y, self._z)
        self._cor_view.setCrosshair(self._x, self._z)
        self._axi_view.setCrosshair(self._x, self._y)

    def _on_clicked(self, x: int, y: int, z: int) -> None:
        self._sx, self._sy, self._sz = x, y, z
        self.seedSet.emit(x, y, z)
        self._x, self._y, self._z = x, y, z
        self._sag_view.setSliceIndex(x)
        self._cor_view.setSliceIndex(y)
        self._axi_view.setSliceIndex(z)
        self._update_crosshairs()
        self.positionChanged.emit(x, y, z)

    def _on_dragged(self, x: int, y: int, z: int) -> None:
        self._sx, self._sy, self._sz = x, y, z
        self.seedSet.emit(x, y, z)

    def _on_scrolled(self, view_axis: int, delta: int) -> None:
        if not self._vol:
            return
        if view_axis == 0:
            self._x = max(0, min(self._vol.nx()-1, self._x + delta))
            self._sag_view.setSliceIndex(self._x)
        elif view_axis == 1:
            self._y = max(0, min(self._vol.ny()-1, self._y + delta))
            self._cor_view.setSliceIndex(self._y)
        else:
            self._z = max(0, min(self._vol.nz()-1, self._z + delta))
            self._axi_view.setSliceIndex(self._z)
        self._update_crosshairs()
        self.positionChanged.emit(self._x, self._y, self._z)
