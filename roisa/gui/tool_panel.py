"""
tool_panel.py — Operator-based right-side control panel.

Operator 0  "Navigation Viewer"  →  Viewer | Data Manager | DICOM Tags
Operator 1  "ROI"                →  Paint  | Segment      | Labels | Save
Operator 2  "Measure"            →  single scrollable page
"""

from __future__ import annotations

import os
from typing import Optional

from PyQt6.QtCore import Qt, QThread, pyqtSignal, QObject
from PyQt6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QDoubleSpinBox,
    QFileDialog, QFrame, QGroupBox, QHBoxLayout, QHeaderView,
    QLabel, QLineEdit, QMessageBox, QPushButton, QScrollArea,
    QSlider, QSpinBox, QStackedWidget, QTabWidget, QTableWidget,
    QTableWidgetItem, QVBoxLayout, QWidget,
)

from ..core.roi_volume import ROIVolume
from ..core.roi_algorithms import ROIAlgorithms
from .histogram_widget import HistogramWidget
from .dicom_tag_widget  import DicomTagWidget


# ── Background segmentation worker ────────────────────────────────────────────

class _SegWorker(QObject):
    finished = pyqtSignal(object)   # passes numpy mask array
    error    = pyqtSignal(str)

    def __init__(self, fn, *args, **kwargs):
        super().__init__()
        self._fn, self._a, self._kw = fn, args, kwargs

    def run(self):
        try:
            self._fn(*self._a, **self._kw)
            self.finished.emit(None)
        except Exception as e:
            self.error.emit(str(e))


# ── Helpers ────────────────────────────────────────────────────────────────────

def _dbl(lo, hi, val, step, dec=2) -> QDoubleSpinBox:
    s = QDoubleSpinBox()
    s.setRange(lo, hi); s.setValue(val)
    s.setSingleStep(step); s.setDecimals(dec)
    return s

def _int(lo, hi, val) -> QSpinBox:
    s = QSpinBox(); s.setRange(lo, hi); s.setValue(val); return s

def _scroll_page(*groups) -> QScrollArea:
    page = QWidget()
    pl   = QVBoxLayout(page)
    pl.setContentsMargins(4, 4, 4, 4); pl.setSpacing(4)
    for g in groups:
        pl.addWidget(g)
    pl.addStretch()
    sa = QScrollArea()
    sa.setWidget(page); sa.setWidgetResizable(True)
    sa.setFrameShape(QFrame.Shape.NoFrame)
    return sa


class ToolPanel(QWidget):
    refreshRequested = pyqtSignal()

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._vol:    Optional[ROIVolume] = None
        self._viewer  = None
        self._seed    = (0, 0, 0)
        self._seed_set = False

        self.setMinimumWidth(280); self.setMaximumWidth(340)
        ml = QVBoxLayout(self)
        ml.setContentsMargins(2, 2, 2, 2); ml.setSpacing(4)

        # ── Operator drop-down ────────────────────────────────────────────────
        self._op_combo = QComboBox()
        self._op_combo.addItems(["Navigation Viewer", "ROI", "Measure"])
        self._op_combo.setStyleSheet(
            "QComboBox{background:#1c2a38;color:#9fcfe8;font-weight:bold;"
            "font-size:12px;padding:5px 10px;border:1px solid #2e5070;"
            "border-radius:4px;}"
            "QComboBox::drop-down{border:none;width:20px;}"
            "QComboBox QAbstractItemView{background:#1c2a38;color:#9fcfe8;"
            "selection-background-color:#2a5070;}")
        ml.addWidget(self._op_combo)

        sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet("border:1px solid #2a3a4a;")
        ml.addWidget(sep)

        # ── Operator stack ────────────────────────────────────────────────────
        self._op_stack = QStackedWidget()
        self._op_stack.addWidget(self._build_nav_viewer_op())
        self._op_stack.addWidget(self._build_roi_op())
        self._op_stack.addWidget(self._build_measure_op())
        ml.addWidget(self._op_stack, 1)

        self._op_combo.currentIndexChanged.connect(self._on_op_changed)

        # ── Status label ──────────────────────────────────────────────────────
        self._status = QLabel()
        self._status.setWordWrap(True)
        self._status.setStyleSheet("color:#aaa;font-size:10px;padding:2px 4px;")
        ml.addWidget(self._status)

    # ══════════════════════════════════════════════════════════════════════════
    # Operator page builders
    # ══════════════════════════════════════════════════════════════════════════

    def _build_nav_viewer_op(self) -> QWidget:
        w = QWidget(); l = QVBoxLayout(w)
        l.setContentsMargins(0,0,0,0); l.setSpacing(0)
        tabs = QTabWidget(); tabs.setDocumentMode(True)

        # Viewer tab
        tabs.addTab(_scroll_page(self._build_nav_group(),
                                  self._build_cine_group(),
                                  self._build_export_group()), "Viewer")
        # Data Manager tab
        dm_page = QWidget(); dm_l = QVBoxLayout(dm_page)
        dm_l.setContentsMargins(4,4,4,4); dm_l.setSpacing(4)
        self._hist_widget = HistogramWidget()
        self._hist_widget.setFixedHeight(90)
        dm_l.addWidget(self._hist_widget)
        dm_l.addWidget(self._build_wl_group())
        dm_l.addWidget(self._build_display_group())
        dm_l.addWidget(self._build_3d_group())
        dm_l.addStretch()
        dm_sa = QScrollArea(); dm_sa.setWidget(dm_page)
        dm_sa.setWidgetResizable(True); dm_sa.setFrameShape(QFrame.Shape.NoFrame)
        tabs.addTab(dm_sa, "Data Manager")

        # DICOM Tags tab
        tag_page = QWidget(); tag_l = QVBoxLayout(tag_page)
        tag_l.setContentsMargins(0,0,0,0); tag_l.setSpacing(0)
        self._tag_widget = DicomTagWidget()
        tag_l.addWidget(self._tag_widget)
        tabs.addTab(tag_page, "DICOM Tags")

        l.addWidget(tabs); return w

    def _build_roi_op(self) -> QWidget:
        w = QWidget(); l = QVBoxLayout(w)
        l.setContentsMargins(0,0,0,0); l.setSpacing(0)
        tabs = QTabWidget(); tabs.setDocumentMode(True)

        tabs.addTab(_scroll_page(self._build_tool_group(),
                                  self._build_edit_group()), "Paint")
        tabs.addTab(_scroll_page(self._build_seg_group(),
                                  self._build_morph_group()), "Segment")
        tabs.addTab(_scroll_page(self._build_label_tools_group(),
                                  self._build_stats_group()), "Labels")
        tabs.addTab(_scroll_page(self._build_io_group(),
                                  self._build_reg_group()), "Save")

        # Auto-switch to Segment tool when Segment tab is activated
        def _on_tab(idx):
            if not self._tool_combo or not self._viewer: return
            if idx == 1:
                from PyQt6.QtCore import QSignalBlocker
                b = QSignalBlocker(self._tool_combo)
                self._tool_combo.setCurrentIndex(2)  # "Segment"
                self._viewer.setMeasureMode(0)
            elif idx == 0:
                self._viewer.setMeasureMode(0)
        tabs.currentChanged.connect(_on_tab)

        l.addWidget(tabs); return w

    def _build_measure_op(self) -> QWidget:
        w = QWidget(); l = QVBoxLayout(w)
        l.setContentsMargins(0,0,0,0); l.setSpacing(0)
        l.addWidget(_scroll_page(self._build_measure_group()))
        return w

    # ══════════════════════════════════════════════════════════════════════════
    # Group builders
    # ══════════════════════════════════════════════════════════════════════════

    def _build_tool_group(self) -> QGroupBox:
        gb = QGroupBox("Tool & Label"); l = QVBoxLayout(gb)
        self._tool_combo = QComboBox()
        self._tool_combo.addItems(["Paint", "Erase", "Segment"])
        self._tool_combo.currentIndexChanged.connect(self._on_tool_changed)
        l.addWidget(self._tool_combo)
        self._label_combo = QComboBox()
        for i in range(1, 11): self._label_combo.addItem(f"Label {i}", i)
        l.addWidget(self._label_combo)
        row = QHBoxLayout(); row.addWidget(QLabel("Radius"))
        self._brush_radius = _int(1, 20, 3); row.addWidget(self._brush_radius)
        l.addLayout(row)
        self._brush_shape = QComboBox()
        self._brush_shape.addItems(["Sphere", "Cylinder", "Cube"]); l.addWidget(self._brush_shape)
        self._two_d_cb = QCheckBox("2D only (current slice)"); l.addWidget(self._two_d_cb)
        return gb

    def _build_nav_group(self) -> QGroupBox:
        gb = QGroupBox("Navigation  (scroll on panel)"); l = QVBoxLayout(gb)
        def row(name):
            r = QHBoxLayout()
            lbl = QLabel(f"{name} 0"); lbl.setFixedWidth(60)
            sl  = QSlider(Qt.Orientation.Horizontal); sl.setRange(0, 255)
            r.addWidget(lbl); r.addWidget(sl); l.addLayout(r)
            return sl, lbl
        self._x_slider, self._x_lbl = row("X (sag)")
        self._y_slider, self._y_lbl = row("Y (cor)")
        self._z_slider, self._z_lbl = row("Z (axi)")
        self._x_slider.valueChanged.connect(self._on_nav_x)
        self._y_slider.valueChanged.connect(self._on_nav_y)
        self._z_slider.valueChanged.connect(self._on_nav_z)
        return gb

    def _build_wl_group(self) -> QGroupBox:
        gb = QGroupBox("Window / Level"); l = QVBoxLayout(gb)
        row = QHBoxLayout()
        self._wl_min = QDoubleSpinBox(); self._wl_min.setRange(-100000,100000)
        self._wl_min.setDecimals(1); self._wl_min.setPrefix("Min ")
        self._wl_max = QDoubleSpinBox(); self._wl_max.setRange(-100000,100000)
        self._wl_max.setDecimals(1); self._wl_max.setPrefix("Max ")
        auto_btn = QPushButton("Auto")
        row.addWidget(self._wl_min); row.addWidget(self._wl_max); row.addWidget(auto_btn)
        l.addLayout(row)

        _WL_PRESETS = [
            ("Brain",40,80),("Stroke",8,32),("Subdural",75,215),
            ("Temporal Bone",700,4000),("Soft Tissue",40,350),
            ("Mediastinum",40,400),("Abdomen",60,400),
            ("Lung",-600,1500),("Bone",700,3000),
            ("MR T1 Brain",500,1000),("MR T2 Brain",800,1600),
        ]
        self._wl_preset = QComboBox(); self._wl_preset.addItem("— CT/MR Preset —")
        for name, c, w in _WL_PRESETS:
            self._wl_preset.addItem(f"{name}  (W:{w} L:{c})", (c, w))
        l.addWidget(self._wl_preset)

        self._wl_min.valueChanged.connect(
            lambda v: (self._vol.set_window(v, self._wl_max.value()),
                       self.refreshRequested.emit()) if self._vol else None)
        self._wl_max.valueChanged.connect(
            lambda v: (self._vol.set_window(self._wl_min.value(), v),
                       self.refreshRequested.emit()) if self._vol else None)
        auto_btn.clicked.connect(self._on_reset_wl)
        self._wl_preset.currentIndexChanged.connect(self._on_wl_preset)
        return gb

    def _build_display_group(self) -> QGroupBox:
        gb = QGroupBox("Display"); l = QVBoxLayout(gb)
        self._cm_combo = QComboBox()
        self._cm_combo.addItems(["Gray", "Hot", "Cool", "Viridis"])
        l.addWidget(self._cm_combo)
        arow = QHBoxLayout(); arow.addWidget(QLabel("Overlay α"))
        self._alpha_sl = QSlider(Qt.Orientation.Horizontal)
        self._alpha_sl.setRange(0,100); self._alpha_sl.setValue(50)
        arow.addWidget(self._alpha_sl); l.addLayout(arow)
        self._interp_cb = QCheckBox("Smooth interpolation"); l.addWidget(self._interp_cb)
        self._info_cb   = QCheckBox("Show info overlay (W/L + position)")
        self._info_cb.setChecked(True); l.addWidget(self._info_cb)
        brow = QHBoxLayout()
        self._show_all_btn  = QPushButton("Show All")
        self._hide_all_btn  = QPushButton("Hide All")
        self._zoom_rst_btn  = QPushButton("Reset Zoom")
        for b in (self._show_all_btn, self._hide_all_btn, self._zoom_rst_btn):
            brow.addWidget(b)
        l.addLayout(brow)
        self._cm_combo.currentIndexChanged.connect(
            lambda i: self._viewer.setColormap(i) if self._viewer else None)
        self._alpha_sl.valueChanged.connect(
            lambda v: self._viewer.setOverlayAlpha(v/100.) if self._viewer else None)
        self._interp_cb.toggled.connect(
            lambda on: self._viewer.setInterpolate(on) if self._viewer else None)
        self._info_cb.toggled.connect(
            lambda on: self._viewer.setShowInfoOverlay(on) if self._viewer else None)
        self._show_all_btn.clicked.connect(
            lambda: self._viewer.setAllLabelsVisible(True) if self._viewer else None)
        self._hide_all_btn.clicked.connect(
            lambda: self._viewer.setAllLabelsVisible(False) if self._viewer else None)
        self._zoom_rst_btn.clicked.connect(
            lambda: self._viewer.resetAllZoom() if self._viewer else None)
        return gb

    def _build_cine_group(self) -> QGroupBox:
        gb = QGroupBox("Cine / Loop"); l = QVBoxLayout(gb)
        top = QHBoxLayout()
        self._cine_btn = QPushButton("▶  Play"); self._cine_btn.setCheckable(True)
        self._cine_btn.setStyleSheet("QPushButton:checked{background:#245;color:white;}")
        top.addWidget(self._cine_btn); top.addWidget(QLabel("FPS:"))
        self._cine_fps = _int(1, 30, 8); self._cine_fps.setMaximumWidth(54)
        top.addWidget(self._cine_fps); l.addLayout(top)
        ax = QHBoxLayout(); ax.addWidget(QLabel("Axis:"))
        self._cine_axis = QComboBox()
        self._cine_axis.addItems(["Sagittal (X)", "Coronal (Y)", "Axial (Z)"])
        self._cine_axis.setCurrentIndex(2)
        ax.addWidget(self._cine_axis); l.addLayout(ax)
        # connections set in setViewer()
        return gb

    def _build_export_group(self) -> QGroupBox:
        gb = QGroupBox("Export"); l = QVBoxLayout(gb)
        l.addWidget(QLabel("Save snapshot PNG:"))
        snap_row = QHBoxLayout()
        self._snap_sag = QPushButton("Sag"); self._snap_cor = QPushButton("Cor")
        self._snap_axi = QPushButton("Axi"); self._snap_vtk = QPushButton("3D")
        for i, b in enumerate((self._snap_sag, self._snap_cor,
                                self._snap_axi, self._snap_vtk)):
            snap_row.addWidget(b)
            b.clicked.connect(lambda _chk, idx=i: self._on_snapshot(idx))
        l.addLayout(snap_row)

        sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet("color:#333;"); l.addWidget(sep)
        l.addWidget(QLabel("Export slice series / movie:"))
        ax_row = QHBoxLayout(); ax_row.addWidget(QLabel("Axis:"))
        self._export_axis = QComboBox()
        self._export_axis.addItems(["Sagittal (X)", "Coronal (Y)", "Axial (Z)"])
        self._export_axis.setCurrentIndex(2)
        ax_row.addWidget(self._export_axis); l.addLayout(ax_row)
        self._export_frames_btn = QPushButton("Export Frames to Folder…")
        self._export_frames_btn.setStyleSheet("background:#1e3a52;color:white;")
        l.addWidget(self._export_frames_btn)
        self._export_movie_btn = QPushButton("Export Movie (MP4)…")
        self._export_movie_btn.setStyleSheet("background:#1e3a52;color:white;")
        l.addWidget(self._export_movie_btn)
        note = QLabel("Movie export requires ffmpeg in PATH.")
        note.setStyleSheet("color:#777;font-size:10px;"); l.addWidget(note)
        self._export_frames_btn.clicked.connect(self._on_export_frames)
        self._export_movie_btn.clicked.connect(self._on_export_movie)
        return gb

    def _build_seg_group(self) -> QGroupBox:
        gb = QGroupBox("Segmentation"); l = QVBoxLayout(gb)
        self._seg_method = QComboBox()
        methods = [
            "Global Threshold","Region Grow (BFS)","Connected Threshold",
            "Neighborhood Connected","Confidence Connected","Flood Fill 2D",
            "Fast Marching","Otsu Threshold","K-Means","Level Set Refine",
            "Watershed","ROI Connected","Remove Small","Connected Components",
            "Fill Holes","Make Shell","Low Pass Smooth","Boolean Op",
        ]
        for i, m in enumerate(methods): self._seg_method.addItem(m, i)
        l.addWidget(self._seg_method)
        self._seed_lbl = QLabel("Seed: click on image (Segment mode)")
        self._seed_lbl.setStyleSheet("color:#f90;font-size:10px;")
        l.addWidget(self._seed_lbl)

        self._seg_stack = QStackedWidget()
        # p0: Global Threshold
        p = QWidget(); pl = QVBoxLayout(p)
        self._t_lo = _dbl(-5000,50000,0,10,1); self._t_lo.setPrefix("Lower: ")
        self._t_hi = _dbl(-5000,50000,1000,10,1); self._t_hi.setPrefix("Upper: ")
        self._t_slice_only = QCheckBox("Slice only")
        self._t_slice_axis = QComboBox(); self._t_slice_axis.addItems(["Axial(Z)","Coronal(Y)","Sagittal(X)"])
        for w in (self._t_lo, self._t_hi, self._t_slice_only, self._t_slice_axis): pl.addWidget(w)
        self._seg_stack.addWidget(p)
        # p1: Region Grow
        p = QWidget(); pl = QVBoxLayout(p)
        self._rg_tol = _dbl(0,5000,50,5,1); self._rg_tol.setPrefix("Tolerance: "); pl.addWidget(self._rg_tol)
        self._seg_stack.addWidget(p)
        # p2: Connected Threshold (reuse t_lo/t_hi)
        p = QWidget(); pl = QVBoxLayout(p)
        pl.addWidget(QLabel("Uses Lower/Upper from Global Threshold."))
        self._seg_stack.addWidget(p)
        # p3: Neighborhood Connected
        p = QWidget(); pl = QVBoxLayout(p)
        self._nb_lo = _dbl(-5000,50000,0,10,1); self._nb_lo.setPrefix("Lower: ")
        self._nb_hi = _dbl(-5000,50000,1000,10,1); self._nb_hi.setPrefix("Upper: ")
        self._nb_r  = _int(1,10,1); self._nb_r.setPrefix("Nbr radius: ")
        for w in (self._nb_lo, self._nb_hi, self._nb_r): pl.addWidget(w)
        self._seg_stack.addWidget(p)
        # p4: Confidence Connected
        p = QWidget(); pl = QVBoxLayout(p)
        self._cc_mult = _dbl(.5,20,2.5,.5); self._cc_mult.setPrefix("Multiplier: ")
        self._cc_iter = _int(1,50,4); self._cc_iter.setPrefix("Iterations: ")
        self._cc_r    = _int(1,10,1); self._cc_r.setPrefix("Radius: ")
        for w in (self._cc_mult, self._cc_iter, self._cc_r): pl.addWidget(w)
        self._seg_stack.addWidget(p)
        # p5: Flood Fill 2D
        p = QWidget(); pl = QVBoxLayout(p)
        self._ff_tol  = _dbl(0,5000,50,5,1); self._ff_tol.setPrefix("Tolerance: ")
        self._ff_axis = QComboBox(); self._ff_axis.addItems(["Axial(Z)","Coronal(Y)","Sagittal(X)"])
        for w in (self._ff_tol, self._ff_axis): pl.addWidget(w)
        self._seg_stack.addWidget(p)
        # p6: Fast Marching
        p = QWidget(); pl = QVBoxLayout(p)
        self._fm_stop = _dbl(1,5000,50,5,1); self._fm_stop.setPrefix("Stopping val: "); pl.addWidget(self._fm_stop)
        self._seg_stack.addWidget(p)
        # p7: Otsu
        p = QWidget(); pl = QVBoxLayout(p)
        self._ot_bins = _int(16,512,128); self._ot_bins.setPrefix("Bins: ")
        self._ot_cls  = _int(1,8,1);     self._ot_cls.setPrefix("Classes: ")
        for w in (self._ot_bins, self._ot_cls): pl.addWidget(w)
        self._seg_stack.addWidget(p)
        # p8: K-Means
        p = QWidget(); pl = QVBoxLayout(p)
        self._km_k = _int(2,20,3); self._km_k.setPrefix("K: "); pl.addWidget(self._km_k)
        self._seg_stack.addWidget(p)
        # p9: Level Set
        p = QWidget(); pl = QVBoxLayout(p)
        self._ls_iter = _int(50,5000,500); self._ls_iter.setPrefix("Iterations: ")
        self._ls_prop = _dbl(0,10,1,.1);   self._ls_prop.setPrefix("Propagation: ")
        self._ls_curv = _dbl(0,10,1,.1);   self._ls_curv.setPrefix("Curvature: ")
        for w in (self._ls_iter, self._ls_prop, self._ls_curv): pl.addWidget(w)
        self._seg_stack.addWidget(p)
        # p10-11: Watershed / ROI Connected
        for txt in ("Morphological watershed.", "Keep component at seed."):
            p = QWidget(); pl = QVBoxLayout(p); pl.addWidget(QLabel(txt))
            self._seg_stack.addWidget(p)
        # p12: Remove Small
        p = QWidget(); pl = QVBoxLayout(p)
        self._rs_min = _int(1,100000,100); self._rs_min.setPrefix("Min voxels: "); pl.addWidget(self._rs_min)
        self._seg_stack.addWidget(p)
        # p13: Connected Components
        p = QWidget(); pl = QVBoxLayout(p)
        self._comp_max = _int(1,255,255); self._comp_max.setPrefix("Max comps: "); pl.addWidget(self._comp_max)
        self._seg_stack.addWidget(p)
        # p14: Fill Holes
        p = QWidget(); pl = QVBoxLayout(p)
        self._fh_axis = QComboBox()
        self._fh_axis.addItems(["3D (all)","Axial(Z)","Coronal(Y)","Sagittal(X)"]); pl.addWidget(self._fh_axis)
        self._seg_stack.addWidget(p)
        # p15: Make Shell
        p = QWidget(); pl = QVBoxLayout(p)
        self._sh_thick = _int(1,10,1); self._sh_thick.setPrefix("Thickness: "); pl.addWidget(self._sh_thick)
        self._seg_stack.addWidget(p)
        # p16: Low Pass Smooth
        p = QWidget(); pl = QVBoxLayout(p)
        self._sm_sig = _dbl(.1,10,1,.1); self._sm_sig.setPrefix("Sigma: "); pl.addWidget(self._sm_sig)
        self._seg_stack.addWidget(p)
        # p17: Boolean Op
        p = QWidget(); pl = QVBoxLayout(p)
        self._bo_lblB = QComboBox()
        for i in range(1,11): self._bo_lblB.addItem(f"Label {i}", i)
        self._bo_lblB.setCurrentIndex(1)
        self._bo_op = QComboBox(); self._bo_op.addItems(["or","and","xor","not","subtract"])
        for w in (QLabel("Label B:"), self._bo_lblB, QLabel("Operation:"), self._bo_op): pl.addWidget(w)
        self._seg_stack.addWidget(p)

        l.addWidget(self._seg_stack)
        self._apply_seg_btn = QPushButton("▶  Apply Segment")
        self._apply_seg_btn.setStyleSheet("background:#2a6;color:white;font-weight:bold;")
        l.addWidget(self._apply_seg_btn)
        self._seg_method.currentIndexChanged.connect(self._seg_stack.setCurrentIndex)
        self._apply_seg_btn.clicked.connect(self._on_apply_seg)
        return gb

    def _build_morph_group(self) -> QGroupBox:
        gb = QGroupBox("Erode / Dilate"); l = QVBoxLayout(gb)
        l.addWidget(QLabel("Erode ←   → Dilate"))
        self._morph_sl = QSlider(Qt.Orientation.Horizontal)
        self._morph_sl.setRange(-5,5); self._morph_sl.setValue(0)
        self._morph_sl.setTickInterval(1)
        self._morph_sl.setTickPosition(QSlider.TickPosition.TicksBelow)
        l.addWidget(self._morph_sl)
        btn = QPushButton("Apply"); l.addWidget(btn)
        btn.clicked.connect(self._on_apply_morph)
        return gb

    def _build_edit_group(self) -> QGroupBox:
        gb = QGroupBox("Edit"); l = QHBoxLayout(gb)
        self._undo_btn  = QPushButton("Undo")
        self._clr_btn   = QPushButton("Clear Label")
        self._clr_all   = QPushButton("Clear All")
        self._undo_btn.setStyleSheet("background:#a60;")
        self._clr_all.setStyleSheet("background:#900;color:white;")
        for b in (self._undo_btn, self._clr_btn, self._clr_all): l.addWidget(b)
        self._undo_btn.clicked.connect(lambda: self._vol.undo() or self.refreshRequested.emit() if self._vol else None)
        self._clr_btn.clicked.connect(lambda: (self._vol.clear_label(self.activeLabel()), self.refreshRequested.emit()) if self._vol else None)
        self._clr_all.clicked.connect(lambda: (self._vol.clear_label(-1), self.refreshRequested.emit()) if self._vol else None)
        return gb

    def _build_label_tools_group(self) -> QGroupBox:
        gb = QGroupBox("Label Tools"); l = QVBoxLayout(gb)
        self._centroid_btn = QPushButton("Snap to Centroid"); l.addWidget(self._centroid_btn)
        prow = QHBoxLayout(); prow.addWidget(QLabel("Propagate:"))
        self._prop_axis = QComboBox(); self._prop_axis.addItems(["X(sag)","Y(cor)","Z(axi)"])
        prow.addWidget(self._prop_axis)
        self._prop_bwd = QPushButton("◀"); self._prop_bwd.setFixedWidth(30)
        self._prop_fwd = QPushButton("▶"); self._prop_fwd.setFixedWidth(30)
        prow.addWidget(self._prop_bwd); prow.addWidget(self._prop_fwd)
        l.addLayout(prow)
        self._surf_btn = QPushButton("Update 3D Surface")
        self._surf_btn.setStyleSheet("background:#226;color:white;"); l.addWidget(self._surf_btn)
        self._centroid_btn.clicked.connect(self._on_snap_centroid)
        self._prop_bwd.clicked.connect(lambda: self._on_propagate(-1))
        self._prop_fwd.clicked.connect(lambda: self._on_propagate(+1))
        self._surf_btn.clicked.connect(lambda: (self._viewer.refreshSurface(self.activeLabel()),
                                                  self._set_status("3D surface updated.")) if self._viewer else None)
        return gb

    def _build_stats_group(self) -> QGroupBox:
        gb = QGroupBox("Label Statistics"); l = QVBoxLayout(gb)
        self._stats_tbl = QTableWidget(0, 5)
        self._stats_tbl.setHorizontalHeaderLabels(["Label","Voxels","Vol(mm³)","Mean","Std"])
        self._stats_tbl.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self._stats_tbl.setMaximumHeight(120)
        self._stats_tbl.horizontalHeader().setStretchLastSection(True)
        l.addWidget(self._stats_tbl)
        csv_btn = QPushButton("Export CSV…"); l.addWidget(csv_btn)
        csv_btn.clicked.connect(self._on_export_csv)
        return gb

    def _build_io_group(self) -> QGroupBox:
        gb = QGroupBox("Save / Load Mask"); l = QVBoxLayout(gb)
        self._save_dir  = QLineEdit(); self._save_dir.setPlaceholderText("/output/dir")
        self._save_file = QLineEdit(); self._save_file.setPlaceholderText("roi_mask.nii.gz")
        self._save_btn  = QPushButton("Save Mask")
        self._save_btn.setStyleSheet("background:#262;color:white;")
        bDir = QPushButton("…"); bDir.setMaximumWidth(30)
        sr = QHBoxLayout(); sr.addWidget(self._save_dir); sr.addWidget(bDir)
        l.addLayout(sr); l.addWidget(self._save_file); l.addWidget(self._save_btn)
        self._load_path = QLineEdit(); self._load_path.setPlaceholderText("/path/to/mask.nii.gz")
        self._load_btn  = QPushButton("Load Mask")
        bLd = QPushButton("…"); bLd.setMaximumWidth(30)
        lr = QHBoxLayout(); lr.addWidget(self._load_path); lr.addWidget(bLd)
        l.addLayout(lr); l.addWidget(self._load_btn)
        bDir.clicked.connect(lambda: self._save_dir.setText(
            QFileDialog.getExistingDirectory(self,"Output directory") or self._save_dir.text()))
        bLd.clicked.connect(lambda: self._load_path.setText(
            QFileDialog.getOpenFileName(self,"Load mask","","NIfTI (*.nii *.nii.gz);;All (*)")[0] or self._load_path.text()))
        self._save_btn.clicked.connect(self._on_save_mask)
        self._load_btn.clicked.connect(self._on_load_mask)
        return gb

    def _build_reg_group(self) -> QGroupBox:
        gb = QGroupBox("Image Registration"); l = QVBoxLayout(gb)
        l.addWidget(QLabel("Moving image (NIfTI or DICOM folder):"))
        self._reg_path = QLineEdit(); self._reg_path.setPlaceholderText("/path/to/moving.nii.gz")
        br = QPushButton("…"); br.setMaximumWidth(30)
        rr = QHBoxLayout(); rr.addWidget(self._reg_path); rr.addWidget(br)
        l.addLayout(rr)
        self._reg_btn = QPushButton("Register & Load")
        self._reg_btn.setStyleSheet("background:#520;color:white;font-weight:bold;")
        l.addWidget(self._reg_btn)
        l.addWidget(QLabel("Rigid registration. Replaces display image;\nmask preserved."))
        br.clicked.connect(lambda: self._reg_path.setText(
            QFileDialog.getOpenFileName(self,"Moving image","","NIfTI (*.nii *.nii.gz);;All (*)")[0]
            or self._reg_path.text()))
        self._reg_btn.clicked.connect(self._on_register)
        return gb

    def _build_3d_group(self) -> QGroupBox:
        gb = QGroupBox("3-D Render"); l = QVBoxLayout(gb)
        mr = QHBoxLayout(); mr.addWidget(QLabel("Mode:"))
        self._vtk_mode = QComboBox(); self._vtk_mode.addItems(["Volume","Surfaces","Both"])
        self._vtk_mode.setCurrentIndex(2)
        mr.addWidget(self._vtk_mode); l.addLayout(mr)
        reset_cam = QPushButton("Reset Camera"); l.addWidget(reset_cam)
        l.addWidget(QLabel("Resample to isotropic voxels:"))
        ir = QHBoxLayout(); ir.addWidget(QLabel("Spacing (mm):"))
        self._iso_spin = QDoubleSpinBox()
        self._iso_spin.setRange(0., 10.); self._iso_spin.setValue(0.)
        self._iso_spin.setSingleStep(.1); self._iso_spin.setDecimals(3)
        self._iso_spin.setSpecialValueText("Auto (min)")
        ir.addWidget(self._iso_spin); l.addLayout(ir)
        iso_btn = QPushButton("Apply Isotropic Resample")
        iso_btn.setStyleSheet("background:#245;color:white;font-weight:bold;")
        l.addWidget(iso_btn)
        hint = QLabel("Replaces display image; mask is reset.\nRecommended before VTK volume render.")
        hint.setStyleSheet("color:#888;font-size:10px;"); l.addWidget(hint)
        self._vtk_mode.currentIndexChanged.connect(
            lambda i: self._viewer.setVtkRenderMode(i) if self._viewer else None)
        reset_cam.clicked.connect(
            lambda: self._viewer.resetVtkCamera() if self._viewer else None)
        iso_btn.clicked.connect(self._on_resample_iso)
        return gb

    def _build_measure_group(self) -> QGroupBox:
        gb = QGroupBox("Measurement Tool"); l = QVBoxLayout(gb)
        l.addWidget(QLabel("Select measurement type below,\nthen click in any slice view."))
        l.addWidget(QLabel("Type:"))
        self._meas_type = QComboBox()
        self._meas_type.addItems(["Ruler (2 clicks)",
                                   "Angle (3 clicks — vertex 2nd)",
                                   "Circle area (drag)"])
        l.addWidget(self._meas_type)
        self._last_meas = QLabel("—")
        self._last_meas.setStyleSheet("color:#aef;font-size:11px;")
        self._last_meas.setWordWrap(True)
        l.addWidget(QLabel("Last result:")); l.addWidget(self._last_meas)
        clr_btn = QPushButton("Clear All Measurements")
        clr_btn.setStyleSheet("background:#500;color:white;"); l.addWidget(clr_btn)
        self._meas_type.currentIndexChanged.connect(self._on_meas_type_changed)
        clr_btn.clicked.connect(
            lambda: self._viewer.clearMeasurements() if self._viewer else None)
        return gb

    # ══════════════════════════════════════════════════════════════════════════
    # Public API
    # ══════════════════════════════════════════════════════════════════════════

    def setVolume(self, vol: ROIVolume) -> None:
        self._vol = vol
        if hasattr(self, '_tag_widget'):
            self._tag_widget.setVolume(vol)
        if hasattr(self, '_hist_widget'):
            self._hist_widget.setVolume(vol)
            self._hist_widget.windowChanged.connect(
                lambda lo, hi: (self._vol.set_window(lo, hi),
                                self.refreshRequested.emit()),
                Qt.ConnectionType.UniqueConnection)
        if not vol or not vol.is_loaded():
            return
        from PyQt6.QtCore import QSignalBlocker
        for sl, lbl, ax, dim in [(self._x_slider, self._x_lbl, "X", vol.nx()),
                                   (self._y_slider, self._y_lbl, "Y", vol.ny()),
                                   (self._z_slider, self._z_lbl, "Z", vol.nz())]:
            b = QSignalBlocker(sl)
            sl.setMaximum(dim - 1); sl.setValue(dim // 2)
            lbl.setText(f"{ax} {dim//2}")
        self._t_lo.setRange(vol.vmin(), vol.vmax()); self._t_lo.setValue(vol.vmin())
        self._t_hi.setRange(vol.vmin(), vol.vmax()); self._t_hi.setValue(vol.vmax())
        self._nb_lo.setRange(vol.vmin(), vol.vmax()); self._nb_lo.setValue(vol.vmin())
        self._nb_hi.setRange(vol.vmin(), vol.vmax()); self._nb_hi.setValue(vol.vmax())
        self.onVolumeLoaded()

    def setViewer(self, viewer) -> None:
        self._viewer = viewer
        if not viewer:
            return
        viewer.measurementAdded.connect(
            lambda s: self._last_meas.setText(s),
            Qt.ConnectionType.UniqueConnection)
        # Cine
        self._cine_btn.toggled.connect(self._on_cine_toggled,
                                        Qt.ConnectionType.UniqueConnection)
        self._cine_fps.valueChanged.connect(
            lambda v: viewer.setCineFps(v),
            Qt.ConnectionType.UniqueConnection)
        self._cine_axis.currentIndexChanged.connect(
            lambda a: viewer.setCineAxis(a),
            Qt.ConnectionType.UniqueConnection)
        if hasattr(self, '_info_cb'):
            viewer.setShowInfoOverlay(self._info_cb.isChecked())

    def activeLabel(self) -> int:
        return self._label_combo.currentData() if hasattr(self, '_label_combo') else 1

    def toolMode(self) -> str:
        op = self._op_combo.currentIndex() if hasattr(self, '_op_combo') else 1
        if op == 0: return ""
        if op == 2: return "measure"
        return self._tool_combo.currentText().lower() if hasattr(self, '_tool_combo') else "paint"

    def brushRadius(self) -> int:
        return self._brush_radius.value() if hasattr(self, '_brush_radius') else 3

    def brushShape(self) -> int:
        return self._brush_shape.currentIndex() if hasattr(self, '_brush_shape') else 0

    def twoDOnly(self) -> bool:
        return self._two_d_cb.isChecked() if hasattr(self, '_two_d_cb') else False

    def refreshStats(self) -> None:
        if not self._vol or not hasattr(self, '_stats_tbl'):
            return
        stats = self._vol.compute_all_stats()
        self._stats_tbl.setRowCount(len(stats))
        for i, s in enumerate(stats):
            for j, v in enumerate([s.label, s.voxel_count,
                                    f"{s.volume_mm3:.1f}",
                                    f"{s.mean_intensity:.1f}",
                                    f"{s.std_intensity:.1f}"]):
                self._stats_tbl.setItem(i, j, QTableWidgetItem(str(v)))

    def onVolumeLoaded(self) -> None:
        if not self._vol: return
        from PyQt6.QtCore import QSignalBlocker
        b1, b2 = QSignalBlocker(self._wl_min), QSignalBlocker(self._wl_max)
        rng = (self._vol.vmin() - 50000, self._vol.vmax() + 50000)
        self._wl_min.setRange(*rng); self._wl_max.setRange(*rng)
        self._wl_min.setValue(self._vol.vmin())
        self._wl_max.setValue(self._vol.vmax())

    # ── Public slots ────────────────────────────────────────────────────────

    def onPositionChanged(self, x: int, y: int, z: int) -> None:
        from PyQt6.QtCore import QSignalBlocker
        for sl, lbl, val, name in [
            (self._x_slider, self._x_lbl, x, "X"),
            (self._y_slider, self._y_lbl, y, "Y"),
            (self._z_slider, self._z_lbl, z, "Z"),
        ]:
            b = QSignalBlocker(sl)
            sl.setValue(val); lbl.setText(f"{name} {val}")

    def onSeedSet(self, x: int, y: int, z: int) -> None:
        self._seed = (x, y, z); self._seed_set = True
        val = self._vol.get_intensity(x, y, z) if self._vol else 0.
        self._seed_lbl.setText(f"Seed: ({x},{y},{z})  val: {val:.1f}")

    # ══════════════════════════════════════════════════════════════════════════
    # Slots
    # ══════════════════════════════════════════════════════════════════════════

    def _set_status(self, msg: str) -> None:
        self._status.setText(msg)

    def _on_op_changed(self, idx: int) -> None:
        self._op_stack.setCurrentIndex(idx)
        if not self._viewer: return
        if idx == 2:
            self._viewer.setMeasureMode(
                self._meas_type.currentIndex() + 1)
        else:
            self._viewer.setMeasureMode(0)

    def _on_tool_changed(self, _idx: int) -> None:
        if self._viewer:
            self._viewer.setMeasureMode(0)

    def _on_meas_type_changed(self, idx: int) -> None:
        if self._op_combo.currentIndex() == 2 and self._viewer:
            self._viewer.setMeasureMode(idx + 1)

    def _on_nav_x(self, v: int) -> None:
        self._x_lbl.setText(f"X {v}")
        if self._viewer: self._viewer.setX(v)

    def _on_nav_y(self, v: int) -> None:
        self._y_lbl.setText(f"Y {v}")
        if self._viewer: self._viewer.setY(v)

    def _on_nav_z(self, v: int) -> None:
        self._z_lbl.setText(f"Z {v}")
        if self._viewer: self._viewer.setZ(v)

    def _on_reset_wl(self) -> None:
        if not self._vol: return
        self._vol.reset_window()
        from PyQt6.QtCore import QSignalBlocker
        b1, b2 = QSignalBlocker(self._wl_min), QSignalBlocker(self._wl_max)
        self._wl_min.setValue(self._vol.vmin())
        self._wl_max.setValue(self._vol.vmax())
        self.refreshRequested.emit()

    def _on_wl_preset(self, idx: int) -> None:
        if idx == 0 or not self._vol: return
        c, w = self._wl_preset.currentData()
        lo, hi = c - w/2., c + w/2.
        self._vol.set_window(lo, hi)
        from PyQt6.QtCore import QSignalBlocker
        b1, b2 = QSignalBlocker(self._wl_min), QSignalBlocker(self._wl_max)
        self._wl_min.setValue(lo); self._wl_max.setValue(hi)
        self.refreshRequested.emit()
        b3 = QSignalBlocker(self._wl_preset); self._wl_preset.setCurrentIndex(0)

    def _on_cine_toggled(self, on: bool) -> None:
        if not self._viewer: return
        if on:
            self._cine_btn.setText("⏹  Stop")
            self._viewer.setCineFps(self._cine_fps.value())
            self._viewer.setCineAxis(self._cine_axis.currentIndex())
            self._viewer.playCine()
        else:
            self._cine_btn.setText("▶  Play")
            self._viewer.stopCine()

    def _on_apply_seg(self) -> None:
        if not self._vol or not self._vol.is_loaded():
            self._set_status("No image loaded."); return
        SEED_METHODS = {1, 2, 3, 4, 5, 6, 11}
        method = self._seg_method.currentData()
        if method in SEED_METHODS and not self._seed_set:
            self._set_status("Set a seed first (click on image in Segment mode).")
            return
        lbl  = self.activeLabel()
        sx, sy, sz = self._seed

        # Run in background thread
        fn_map = {
            0:  lambda: ROIAlgorithms.threshold_segment(
                    self._vol, self._t_lo.value(), self._t_hi.value(), lbl,
                    axis=([2,1,0][self._t_slice_axis.currentIndex()]
                          if self._t_slice_only.isChecked() else -1),
                    slice_idx=(self._viewer.z() if self._t_slice_only.isChecked()
                               and self._t_slice_axis.currentIndex()==0
                               else self._viewer.y() if self._t_slice_only.isChecked()
                               and self._t_slice_axis.currentIndex()==1
                               else self._viewer.x() if self._t_slice_only.isChecked()
                               else -1) if self._viewer else -1),
            1:  lambda: ROIAlgorithms.region_grow(self._vol, sx, sy, sz, self._rg_tol.value(), lbl),
            2:  lambda: ROIAlgorithms.connected_threshold(self._vol, sx, sy, sz, self._t_lo.value(), self._t_hi.value(), lbl),
            3:  lambda: ROIAlgorithms.neighborhood_connected(self._vol, sx, sy, sz, self._nb_lo.value(), self._nb_hi.value(), self._nb_r.value(), lbl),
            4:  lambda: ROIAlgorithms.confidence_connected(self._vol, sx, sy, sz, self._cc_mult.value(), self._cc_iter.value(), self._cc_r.value(), lbl),
            5:  lambda: ROIAlgorithms.flood_fill_2d(self._vol, sx, sy, sz, [2,1,0][self._ff_axis.currentIndex()], self._ff_tol.value(), lbl),
            6:  lambda: ROIAlgorithms.fast_marching(self._vol, sx, sy, sz, self._fm_stop.value(), lbl),
            7:  lambda: ROIAlgorithms.otsu_threshold(self._vol, lbl, self._ot_bins.value(), self._ot_cls.value()),
            8:  lambda: ROIAlgorithms.kmeans_cluster(self._vol, self._km_k.value(), lbl),
            9:  lambda: ROIAlgorithms.level_set_refine(self._vol, lbl, self._ls_iter.value(), self._ls_prop.value(), self._ls_curv.value()),
            10: lambda: ROIAlgorithms.watershed(self._vol, lbl),
            11: lambda: ROIAlgorithms.roi_connected(self._vol, sx, sy, sz, lbl, lbl),
            12: lambda: ROIAlgorithms.remove_small_components(self._vol, lbl, self._rs_min.value()),
            13: lambda: ROIAlgorithms.connected_components(self._vol, lbl, self._comp_max.value()),
            14: lambda: ROIAlgorithms.fill_holes(self._vol, lbl, [-1,2,1,0][self._fh_axis.currentIndex()]),
            15: lambda: ROIAlgorithms.make_shell(self._vol, lbl, self._sh_thick.value()),
            16: lambda: ROIAlgorithms.low_pass_smooth(self._vol, lbl, self._sm_sig.value()),
            17: lambda: ROIAlgorithms.boolean_op(self._vol, lbl, self._bo_lblB.currentData(), self._bo_op.currentText(), lbl),
        }
        fn = fn_map.get(method)
        if fn is None:
            self._set_status("Unknown method."); return

        self._apply_seg_btn.setEnabled(False)
        self._set_status("Running…")

        self._seg_thread = QThread()
        self._seg_worker = _SegWorker(fn)
        self._seg_worker.moveToThread(self._seg_thread)
        self._seg_thread.started.connect(self._seg_worker.run)
        self._seg_worker.finished.connect(self._on_seg_done)
        self._seg_worker.error.connect(self._on_seg_error)
        self._seg_worker.finished.connect(self._seg_thread.quit)
        self._seg_worker.error.connect(self._seg_thread.quit)
        self._seg_thread.start()

    def _on_seg_done(self, _) -> None:
        self._apply_seg_btn.setEnabled(True)
        self._set_status("Done.")
        self.refreshRequested.emit()

    def _on_seg_error(self, msg: str) -> None:
        self._apply_seg_btn.setEnabled(True)
        self._set_status(f"Error: {msg}")
        QMessageBox.critical(self, "Segmentation error", msg)

    def _on_apply_morph(self) -> None:
        if not self._vol: return
        r = self._morph_sl.value()
        if r == 0: self._set_status("Radius=0, no change."); return
        try:
            ROIAlgorithms.morph_erode_dilate(self._vol, self.activeLabel(), r)
            self.refreshRequested.emit(); self._set_status("Morph applied.")
        except Exception as e:
            self._set_status(f"Morph error: {e}")

    def _on_snap_centroid(self) -> None:
        if not self._vol or not self._viewer: return
        c = self._vol.label_centroid(self.activeLabel())
        self._viewer.setX(int(round(c[0])))
        self._viewer.setY(int(round(c[1])))
        self._viewer.setZ(int(round(c[2])))

    def _on_propagate(self, direction: int) -> None:
        if not self._vol or not self._viewer: return
        ac = self._prop_axis.currentIndex()
        idx = (self._viewer.x() if ac == 0
               else self._viewer.y() if ac == 1
               else self._viewer.z())
        n = self._vol.propagate_label(self.activeLabel(), ac, idx, direction)
        self._set_status(f"Propagated {n} voxels.")
        self.refreshRequested.emit()

    def _on_export_csv(self) -> None:
        if not self._vol: return
        path, _ = QFileDialog.getSaveFileName(self,"Export CSV","roisa_stats.csv","CSV (*.csv)")
        if not path: return
        ok = self._vol.export_stats_csv(path)
        self._set_status("Exported: " + path if ok else "CSV export failed.")

    def _on_save_mask(self) -> None:
        if not self._vol: self._set_status("No image."); return
        d = self._save_dir.text().strip()
        if not d:
            d = QFileDialog.getExistingDirectory(self, "Output directory")
            if not d: return
            self._save_dir.setText(d)
        fn = self._save_file.text().strip() or "roi_mask.nii.gz"
        out = os.path.join(d, fn)
        self._set_status("Saved: " + out if self._vol.save_mask(out) else "Save failed.")

    def _on_load_mask(self) -> None:
        if not self._vol: self._set_status("No image."); return
        p = self._load_path.text().strip()
        if not p: return
        if self._vol.load_mask(p):
            self.refreshRequested.emit(); self._set_status("Mask loaded.")
        else:
            self._set_status("Load failed.")

    def _on_register(self) -> None:
        if not self._vol or not self._vol.is_loaded():
            self._set_status("Load a fixed image first."); return
        p = self._reg_path.text().strip()
        if not p: self._set_status("No moving image path."); return
        self._set_status("Registering… (~30 s)")
        QApplication.processEvents()
        if self._vol.load_registered_image(p):
            self.onVolumeLoaded()
            if hasattr(self, '_hist_widget'): self._hist_widget.setVolume(self._vol)
            self.refreshRequested.emit()
            self._set_status("Registration complete.")
        else:
            self._set_status("Registration failed — see console.")

    def _on_resample_iso(self) -> None:
        if not self._vol or not self._vol.is_loaded():
            self._set_status("No image loaded."); return
        sp = float(self._iso_spin.value())
        self._set_status("Resampling…"); QApplication.processEvents()
        if self._vol.resample_to_isotropic(sp):
            self.onVolumeLoaded()
            if hasattr(self, '_hist_widget'): self._hist_widget.setVolume(self._vol)
            if self._viewer:
                self._viewer.setVolume(self._vol); self._viewer.refresh()
            self.refreshRequested.emit()
            self._set_status(f"Resampled → {self._vol.nx()}×{self._vol.ny()}×{self._vol.nz()}")
        else:
            self._set_status("Resample failed.")

    # ── Export ─────────────────────────────────────────────────────────────────

    def _on_snapshot(self, view_idx: int) -> None:
        if not self._viewer: self._set_status("No viewer."); return
        names = ["sagittal", "coronal", "axial", "3D"]
        path, _ = QFileDialog.getSaveFileName(
            self, f"Save {names[view_idx]} snapshot",
            f"roisa_{names[view_idx]}.png",
            "PNG (*.png);;JPEG (*.jpg);;All files (*)")
        if not path: return
        grab_fns = [self._viewer.grabSagittal, self._viewer.grabCoronal,
                    self._viewer.grabAxial,    self._viewer.grabVtk]
        px = grab_fns[view_idx]()
        if px.save(path):
            self._set_status(f"Saved: {os.path.basename(path)}")
        else:
            self._set_status("Snapshot save failed.")

    def _on_export_frames(self) -> None:
        if not self._vol or not self._viewer:
            self._set_status("No image loaded."); return
        d = QFileDialog.getExistingDirectory(self, "Output folder for frames")
        if not d: return
        axis  = self._export_axis.currentIndex()
        total = (self._vol.nx() if axis==0
                 else self._vol.ny() if axis==1
                 else self._vol.nz())
        ox, oy, oz = self._viewer.x(), self._viewer.y(), self._viewer.z()
        grab = [self._viewer.grabSagittal,
                self._viewer.grabCoronal,
                self._viewer.grabAxial][axis]
        nav  = [self._viewer.setX, self._viewer.setY, self._viewer.setZ][axis]
        saved = 0
        for i in range(total):
            nav(i); QApplication.processEvents()
            if grab().save(os.path.join(d, f"frame_{i:04d}.png")):
                saved += 1
            if i % 20 == 0:
                self._set_status(f"Exporting {i+1}/{total}…")
        self._viewer.setX(ox); self._viewer.setY(oy); self._viewer.setZ(oz)
        self._set_status(f"Exported {saved}/{total} frames → {d}")

    def _on_export_movie(self) -> None:
        if not self._vol or not self._viewer:
            self._set_status("No image loaded."); return
        out_path, _ = QFileDialog.getSaveFileName(
            self, "Export Movie", "roisa_movie.mp4",
            "MP4 (*.mp4);;AVI (*.avi);;All files (*)")
        if not out_path: return
        import tempfile, subprocess
        tmp = tempfile.mkdtemp(prefix="roisa_movie_")
        axis  = self._export_axis.currentIndex()
        total = (self._vol.nx() if axis==0
                 else self._vol.ny() if axis==1
                 else self._vol.nz())
        fps = self._cine_fps.value()
        ox, oy, oz = self._viewer.x(), self._viewer.y(), self._viewer.z()
        grab = [self._viewer.grabSagittal,
                self._viewer.grabCoronal,
                self._viewer.grabAxial][axis]
        nav  = [self._viewer.setX, self._viewer.setY, self._viewer.setZ][axis]
        for i in range(total):
            nav(i); QApplication.processEvents()
            grab().save(os.path.join(tmp, f"frame_{i:04d}.png"))
            if i % 20 == 0:
                self._set_status(f"Rendering {i+1}/{total}…")
        self._viewer.setX(ox); self._viewer.setY(oy); self._viewer.setZ(oz)
        self._set_status("Encoding with ffmpeg…"); QApplication.processEvents()
        try:
            result = subprocess.run(
                ["ffmpeg", "-y", "-framerate", str(fps),
                 "-i", os.path.join(tmp, "frame_%04d.png"),
                 "-c:v", "libx264", "-pix_fmt", "yuv420p", out_path],
                capture_output=True, timeout=120)
            import shutil; shutil.rmtree(tmp, ignore_errors=True)
            if result.returncode == 0:
                self._set_status(f"Movie saved: {os.path.basename(out_path)}")
            else:
                self._set_status(f"ffmpeg failed (exit {result.returncode}). Frames in: {tmp}")
        except FileNotFoundError:
            self._set_status(f"ffmpeg not found in PATH. Frames saved to: {tmp}")
        except Exception as e:
            self._set_status(f"Movie error: {e}")
