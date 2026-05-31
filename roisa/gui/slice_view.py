"""
slice_view.py — Pure-painter 2-D slice widget.

Responsibilities
----------------
- Renders one 2-D numpy slice as a QImage (window/level + colormap + mask overlay)
- Draws crosshairs, measurement overlays, and info text
- Emits voxel-space signals for click / drag / scroll / measurements
- Zoom (Ctrl+scroll or pinch) and right-button pan
"""

from __future__ import annotations

import math
from enum import IntEnum
from typing import List, Optional, Tuple

import numpy as np
from PyQt6.QtCore import QPoint, QPointF, QRectF, Qt, pyqtSignal
from PyQt6.QtGui import QColor, QFont, QImage, QPainter, QPen, QPixmap
from PyQt6.QtWidgets import QWidget

from ..core.roi_volume import LABEL_COLORS


class MeasureMode(IntEnum):
    NONE   = 0
    RULER  = 1
    ANGLE  = 2
    CIRCLE = 3


class SliceView(QWidget):
    # ── Signals ────────────────────────────────────────────────────────────────
    sliceClicked     = pyqtSignal(int, int, int)  # x, y, z voxel
    sliceDragged     = pyqtSignal(int, int, int)
    sliceReleased    = pyqtSignal()
    scrolled         = pyqtSignal(int)            # +1 / -1
    measurementAdded = pyqtSignal(str)            # human-readable result

    # ── Construction ──────────────────────────────────────────────────────────

    def __init__(self, axis: int, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._axis      = axis   # 0=sag  1=cor  2=axi
        self._vol       = None
        self._slice_idx = 0

        # Display
        self._colormap       = 0      # 0=gray 1=hot 2=cool 3=viridis (base)
        self._base_visible   = True   # REF base layer composited or blacked out
        self._overlay_alpha  = 0.5
        self._interpolate    = False
        self._show_info      = True

        # Fusion overlays: list of dicts
        #   {arr:(nz,ny,nx) on REF grid, colormap:int, alpha:float,
        #    wmin:float, wmax:float}
        self._overlays: List[dict] = []

        # Crosshair (voxel coords on the in-plane axes)
        self._ch = -1   # horizontal-axis voxel index
        self._cv = -1   # vertical-axis voxel index

        # Zoom / pan
        self._zoom  = 1.0
        self._pan_x = 0.0
        self._pan_y = 0.0

        # Mouse state
        self._left_btn   = False
        self._right_btn  = False
        self._last_rpos: Optional[QPoint] = None

        # Measurements
        self._measure_mode  = MeasureMode.NONE
        self._pending_pts:  List[QPointF] = []
        self._circle_start: Optional[QPointF] = None
        self._circle_cur:   Optional[QPointF] = None
        self._finished: List[Tuple[MeasureMode, List[QPointF], str]] = []

        self.setMouseTracking(False)
        self.setMinimumSize(100, 100)
        self.setStyleSheet("background:#000;")
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent)

    # ── Public API ─────────────────────────────────────────────────────────────

    def setVolume(self, vol) -> None:
        self._vol = vol;  self.update()

    def setSliceIndex(self, idx: int) -> None:
        self._slice_idx = idx;  self.update()

    def setCrosshair(self, h: int, v: int) -> None:
        self._ch, self._cv = h, v;  self.update()

    def setColormap(self, cm: int) -> None:
        self._colormap = cm;  self.update()

    def colormap(self) -> int:
        return self._colormap

    def setBaseVisible(self, on: bool) -> None:
        self._base_visible = on;  self.update()

    def setOverlays(self, overlays: List[dict]) -> None:
        """Set the fusion overlay layers (each on the REF grid)."""
        self._overlays = overlays or []
        self.update()

    def setOverlayAlpha(self, a: float) -> None:
        self._overlay_alpha = a;  self.update()

    def setInterpolate(self, on: bool) -> None:
        self._interpolate = on;  self.update()

    def setShowInfoOverlay(self, on: bool) -> None:
        self._show_info = on;  self.update()

    def setAllLabelsVisible(self, v: bool) -> None:
        if self._vol:
            self._vol.set_all_labels_visible(v)
        self.update()

    def setMeasureMode(self, mode: int) -> None:
        self._measure_mode  = MeasureMode(mode)
        self._pending_pts.clear()
        self._circle_start = self._circle_cur = None
        self.update()

    def clearMeasurements(self) -> None:
        self._finished.clear()
        self._pending_pts.clear()
        self._circle_start = self._circle_cur = None
        self.update()

    def resetZoom(self) -> None:
        self._zoom = 1.0;  self._pan_x = self._pan_y = 0.0;  self.update()

    # ── Coordinate helpers ─────────────────────────────────────────────────────

    def _slice_shape(self) -> Tuple[int, int]:
        """(rows, cols) of the displayed slice."""
        if not self._vol or not self._vol.is_loaded():
            return 1, 1
        if   self._axis == 2: return self._vol.ny(), self._vol.nx()
        elif self._axis == 1: return self._vol.nz(), self._vol.nx()
        else:                 return self._vol.nz(), self._vol.ny()

    def _transform(self) -> Tuple[float, float, float]:
        """Returns (scale, ox, oy): slice_pixel * scale + offset = widget_pixel."""
        rows, cols = self._slice_shape()
        w, h = max(1, self.width()), max(1, self.height())
        scale = min(w / max(1, cols), h / max(1, rows)) * self._zoom
        ox = (w - cols * scale) / 2.0 + self._pan_x
        oy = (h - rows * scale) / 2.0 + self._pan_y
        return scale, ox, oy

    def _widget_to_slice(self, wx: float, wy: float) -> Tuple[float, float]:
        scale, ox, oy = self._transform()
        return (wx - ox) / scale, (wy - oy) / scale

    def _slice_to_voxel(self, sc: float, sr: float) -> Tuple[int, int, int]:
        rows, cols = self._slice_shape()
        c = int(max(0, min(cols - 1, round(sc))))
        r = int(max(0, min(rows - 1, round(sr))))
        if   self._axis == 2: return c, r, self._slice_idx
        elif self._axis == 1: return c, self._slice_idx, r
        else:                 return self._slice_idx, c, r

    # ── Rendering ──────────────────────────────────────────────────────────────

    def paintEvent(self, _event) -> None:
        p = QPainter(self)
        p.fillRect(self.rect(), QColor(0, 0, 0))

        if not self._vol or not self._vol.is_loaded():
            p.setPen(QColor(70, 70, 70))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "No image loaded")
            return

        img_sl = self._vol.get_image_slice(self._axis, self._slice_idx)
        msk_sl = self._vol.get_mask_slice(self._axis, self._slice_idx)
        rgba   = self._build_rgba(img_sl, msk_sl)
        rows, cols = rgba.shape[:2]

        cont = np.ascontiguousarray(rgba)
        qi   = QImage(cont.data, cols, rows, 4 * cols,
                      QImage.Format.Format_RGBA8888)
        # keep data alive while QImage exists
        qi._data = cont

        scale, ox, oy = self._transform()
        sw, sh = max(1, int(cols * scale)), max(1, int(rows * scale))
        mode = (Qt.TransformationMode.SmoothTransformation
                if self._interpolate
                else Qt.TransformationMode.FastTransformation)
        pix = QPixmap.fromImage(qi).scaled(
                sw, sh,
                Qt.AspectRatioMode.IgnoreAspectRatio,
                mode)
        p.drawPixmap(int(ox), int(oy), pix)

        self._paint_crosshair(p)
        self._paint_measurements(p)
        if self._show_info:
            self._paint_info(p)

    # ── RGBA construction ──────────────────────────────────────────────────────

    @staticmethod
    def _apply_colormap(gray: np.ndarray, cm: int) -> np.ndarray:
        if cm == 0:  # gray
            return np.stack([gray, gray, gray], -1)
        if cm == 1:  # hot
            r = np.clip(gray.astype(np.int32) * 3,       0, 255).astype(np.uint8)
            g = np.clip(gray.astype(np.int32) * 3 - 255, 0, 255).astype(np.uint8)
            b = np.clip(gray.astype(np.int32) * 3 - 510, 0, 255).astype(np.uint8)
            return np.stack([r, g, b], -1)
        if cm == 2:  # cool
            return np.stack([gray, (255 - gray).astype(np.uint8),
                             np.full_like(gray, 255)], -1)
        # viridis approximation
        t = gray.astype(np.float32) / 255.
        r = np.clip(( 0.267 + 0.004*t + 2.334*t**2 - 2.545*t**3 + 1.296*t**4)*255,0,255).astype(np.uint8)
        g = np.clip(( 0.005 + 1.432*t + 0.072*t**2 - 0.916*t**3 + 0.378*t**4)*255,0,255).astype(np.uint8)
        b = np.clip(( 0.329 + 1.117*t - 2.384*t**2 + 2.209*t**3 - 0.815*t**4)*255,0,255).astype(np.uint8)
        return np.stack([r, g, b], -1)

    def _overlay_slice(self, arr: np.ndarray) -> np.ndarray:
        """Extract the current 2-D slice from an overlay array (REF grid)."""
        if   self._axis == 2: return arr[self._slice_idx, :, :]
        elif self._axis == 1: return arr[:, self._slice_idx, :]
        else:                 return arr[:, :, self._slice_idx]

    def _build_rgba(self, img_sl: np.ndarray,
                    msk_sl: np.ndarray) -> np.ndarray:
        # ── Base layer (REF) — grayscale or its own colormap ──────────────────
        if self._base_visible:
            wmin, wmax = self._vol.vmin(), self._vol.vmax()
            wrange = max(wmax - wmin, 1e-6)
            norm   = np.clip((img_sl - wmin) / wrange, 0., 1.)
            gray   = (norm * 255).astype(np.uint8)
            rgb    = self._apply_colormap(gray, self._colormap).astype(np.float32)
        else:
            rgb    = np.zeros((*img_sl.shape, 3), np.float32)

        # ── Fusion overlays — composite on top with per-layer colormap ────────
        # Each overlay's effective opacity is modulated by its windowed
        # intensity, so background stays transparent and hotspots show
        # strongly (the classic PET-on-CT look).
        base_rows, base_cols = rgb.shape[:2]
        for ov in self._overlays:
            arr = ov.get("arr")
            if arr is None:
                continue
            try:
                osl = self._overlay_slice(arr)
            except (IndexError, ValueError):
                continue
            if osl.shape != (base_rows, base_cols):
                continue
            owmin = ov.get("wmin", 0.0)
            owmax = ov.get("wmax", 1.0)
            orange = max(owmax - owmin, 1e-6)
            onorm  = np.clip((osl - owmin) / orange, 0., 1.)
            ogray  = (onorm * 255).astype(np.uint8)
            orgb   = self._apply_colormap(ogray, ov.get("colormap", 1)).astype(np.float32)
            eff = (ov.get("alpha", 0.6) * onorm)[..., None]   # (h,w,1)
            rgb = rgb * (1.0 - eff) + orgb * eff

        # ── Mask label overlay (always topmost) ──────────────────────────────
        alpha = self._overlay_alpha
        for lbl in range(1, len(LABEL_COLORS)):
            color = LABEL_COLORS[lbl]
            if color is None:
                continue
            if self._vol and not self._vol.label_visible(lbl):
                continue
            px = msk_sl == lbl
            if not px.any():
                continue
            r, g, b = color
            rgb[px, 0] = np.clip(rgb[px, 0] * (1 - alpha) + r * alpha, 0, 255)
            rgb[px, 1] = np.clip(rgb[px, 1] * (1 - alpha) + g * alpha, 0, 255)
            rgb[px, 2] = np.clip(rgb[px, 2] * (1 - alpha) + b * alpha, 0, 255)

        h, w = rgb.shape[:2]
        rgba = np.empty((h, w, 4), np.uint8)
        rgba[:, :, :3] = rgb.astype(np.uint8)
        rgba[:, :,  3] = 255
        return rgba

    # ── Overlay painters ───────────────────────────────────────────────────────

    def _paint_crosshair(self, p: QPainter) -> None:
        if self._ch < 0 or self._cv < 0:
            return
        scale, ox, oy = self._transform()
        col_px = ox + (self._ch + .5) * scale
        row_px = oy + (self._cv + .5) * scale
        pen = QPen(QColor(0, 200, 255, 160), 1)
        p.setPen(pen)
        w, h = self.width(), self.height()
        p.drawLine(0, int(row_px), w, int(row_px))
        p.drawLine(int(col_px), 0, int(col_px), h)

    def _paint_measurements(self, p: QPainter) -> None:
        pen = QPen(QColor(255, 220, 0), 1)
        p.setPen(pen)
        f = QFont(); f.setPointSize(8); p.setFont(f)

        for mode, pts, label in self._finished:
            if mode == MeasureMode.RULER and len(pts) == 2:
                p.drawLine(pts[0].toPoint(), pts[1].toPoint())
                p.drawText((pts[0] + pts[1]) / 2, label)
            elif mode == MeasureMode.ANGLE and len(pts) == 3:
                p.drawLine(pts[0].toPoint(), pts[1].toPoint())
                p.drawLine(pts[1].toPoint(), pts[2].toPoint())
                p.drawText(pts[1].toPoint(), label)
            elif mode == MeasureMode.CIRCLE and len(pts) == 2:
                self._draw_circle(p, pts[0], pts[1])
                p.drawText(pts[0].toPoint(), label)

        # In-progress circle preview
        if (self._measure_mode == MeasureMode.CIRCLE
                and self._circle_start and self._circle_cur):
            self._draw_circle(p, self._circle_start, self._circle_cur)

    @staticmethod
    def _draw_circle(p: QPainter, c: QPointF, e: QPointF) -> None:
        dx = e.x() - c.x(); dy = e.y() - c.y()
        r  = math.hypot(dx, dy)
        p.drawEllipse(QRectF(c.x() - r, c.y() - r, 2*r, 2*r))

    def _paint_info(self, p: QPainter) -> None:
        if not self._vol:
            return
        f = QFont(); f.setPointSize(8); p.setFont(f)
        p.setPen(QColor(210, 210, 210))
        ir = self.rect().adjusted(0, 0, -4, -4)

        ww = self._vol.vmax() - self._vol.vmin()
        wl = (self._vol.vmin() + self._vol.vmax()) * .5
        p.drawText(ir,
                   Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignRight,
                   f"W:{int(ww)}  L:{int(wl)}")

        tot = (self._vol.nx() if self._axis == 0
               else self._vol.ny() if self._axis == 1
               else self._vol.nz())
        pos = self._slice_idx * self._vol.voxel_spacing_mm()
        p.drawText(ir.adjusted(4, 0, 0, 0),
                   Qt.AlignmentFlag.AlignBottom | Qt.AlignmentFlag.AlignLeft,
                   f"{self._slice_idx+1}/{tot}  •  {pos:.1f} mm")

    # ── Mouse events ───────────────────────────────────────────────────────────

    def mousePressEvent(self, e) -> None:
        if e.button() == Qt.MouseButton.LeftButton:
            self._left_btn = True
            pos = e.position()
            if self._measure_mode == MeasureMode.NONE:
                sc, sr = self._widget_to_slice(pos.x(), pos.y())
                self.sliceClicked.emit(*self._slice_to_voxel(sc, sr))
            elif self._measure_mode == MeasureMode.CIRCLE:
                self._circle_start = QPointF(pos.x(), pos.y())
                self._circle_cur   = QPointF(pos.x(), pos.y())
            else:
                self._pending_pts.append(QPointF(pos.x(), pos.y()))
                self._try_complete_measure()

        elif e.button() == Qt.MouseButton.RightButton:
            self._right_btn  = True
            self._last_rpos  = e.position().toPoint()

    def mouseMoveEvent(self, e) -> None:
        if self._left_btn:
            pos = e.position()
            if self._measure_mode == MeasureMode.NONE:
                sc, sr = self._widget_to_slice(pos.x(), pos.y())
                self.sliceDragged.emit(*self._slice_to_voxel(sc, sr))
            elif self._measure_mode == MeasureMode.CIRCLE and self._circle_start:
                self._circle_cur = QPointF(pos.x(), pos.y())
                self.update()
        elif self._right_btn and self._last_rpos:
            cur = e.position().toPoint()
            self._pan_x += cur.x() - self._last_rpos.x()
            self._pan_y += cur.y() - self._last_rpos.y()
            self._last_rpos = cur
            self.update()

    def mouseReleaseEvent(self, e) -> None:
        if e.button() == Qt.MouseButton.LeftButton:
            if (self._measure_mode == MeasureMode.CIRCLE
                    and self._circle_start and self._circle_cur):
                pts = [QPointF(self._circle_start), QPointF(self._circle_cur)]
                label = self._circle_label(pts)
                self._finished.append((MeasureMode.CIRCLE, pts, label))
                self.measurementAdded.emit(label)
                self._circle_start = self._circle_cur = None
            self._left_btn = False
            if self._measure_mode == MeasureMode.NONE:
                self.sliceReleased.emit()
            self.update()
        elif e.button() == Qt.MouseButton.RightButton:
            self._right_btn = False
            self._last_rpos = None

    def wheelEvent(self, e) -> None:
        delta = 1 if e.angleDelta().y() > 0 else -1
        if e.modifiers() & Qt.KeyboardModifier.ControlModifier:
            f = 1.1 if delta > 0 else 1./1.1
            self._zoom = max(.1, min(20., self._zoom * f))
            self.update()
        else:
            self.scrolled.emit(delta)

    # ── Measurement helpers ────────────────────────────────────────────────────

    def _try_complete_measure(self) -> None:
        m, pts = self._measure_mode, self._pending_pts
        if m == MeasureMode.RULER and len(pts) >= 2:
            label = self._ruler_label(pts[:2])
            self._finished.append((MeasureMode.RULER, list(pts[:2]), label))
            self.measurementAdded.emit(label)
            self._pending_pts.clear()
            self.update()
        elif m == MeasureMode.ANGLE and len(pts) >= 3:
            label = self._angle_label(pts[:3])
            self._finished.append((MeasureMode.ANGLE, list(pts[:3]), label))
            self.measurementAdded.emit(label)
            self._pending_pts.clear()
            self.update()

    def _px_dist_mm(self, p1: QPointF, p2: QPointF) -> float:
        if not self._vol:
            return 0.
        sc1, sr1 = self._widget_to_slice(p1.x(), p1.y())
        sc2, sr2 = self._widget_to_slice(p2.x(), p2.y())
        sp = self._vol.spacing_xyz()
        if   self._axis == 2: sx, sy = sp[0], sp[1]
        elif self._axis == 1: sx, sy = sp[0], sp[2]
        else:                 sx, sy = sp[1], sp[2]
        return math.hypot((sc2 - sc1)*sx, (sr2 - sr1)*sy)

    def _ruler_label(self, pts: List[QPointF]) -> str:
        return f"{self._px_dist_mm(pts[0], pts[1]):.1f} mm"

    def _angle_label(self, pts: List[QPointF]) -> str:
        v1 = pts[0] - pts[1]; v2 = pts[2] - pts[1]
        dot  = v1.x()*v2.x() + v1.y()*v2.y()
        mag1 = math.hypot(v1.x(), v1.y())
        mag2 = math.hypot(v2.x(), v2.y())
        if mag1 * mag2 < 1e-10:
            return "0.0°"
        angle = math.degrees(math.acos(max(-1., min(1., dot / (mag1*mag2)))))
        return f"{angle:.1f}°"

    def _circle_label(self, pts: List[QPointF]) -> str:
        r_widget = math.hypot(pts[1].x()-pts[0].x(), pts[1].y()-pts[0].y())
        scale, _, _ = self._transform()
        r_slice = r_widget / max(scale, 1e-6)
        sp = self._vol.spacing_xyz() if self._vol else (1., 1., 1.)
        sx = sp[0] if self._axis != 0 else sp[1]
        r_mm = r_slice * sx
        return f"r={r_mm:.1f} mm  A={math.pi*r_mm*r_mm:.1f} mm²"
