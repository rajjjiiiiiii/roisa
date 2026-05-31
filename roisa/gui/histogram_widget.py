"""
histogram_widget.py — Intensity histogram with draggable W/L bars.
"""

from __future__ import annotations

from typing import Optional

import numpy as np
from PyQt6.QtCore import QPoint, QRectF, Qt, pyqtSignal
from PyQt6.QtGui import QColor, QLinearGradient, QPainter, QPen
from PyQt6.QtWidgets import QWidget


class HistogramWidget(QWidget):
    windowChanged = pyqtSignal(float, float)   # lo, hi

    _BAR_W = 6   # half-width of drag handle in pixels

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._bins:   Optional[np.ndarray] = None   # counts
        self._edges:  Optional[np.ndarray] = None   # N+1 edges
        self._lo:     float = 0.
        self._hi:     float = 1.
        self._vmin:   float = 0.
        self._vmax:   float = 1.
        self._dragging: Optional[str] = None   # 'lo' | 'hi' | None
        self.setMinimumHeight(70)
        self.setCursor(Qt.CursorShape.CrossCursor)

    # ── Public API ─────────────────────────────────────────────────────────────

    def setVolume(self, vol) -> None:
        if vol is None or not vol.is_loaded():
            self._bins = self._edges = None
            self.update()
            return
        flat = vol.arr.ravel()
        self._vmin, self._vmax = float(flat.min()), float(flat.max())
        self._lo, self._hi = vol.vmin(), vol.vmax()
        counts, edges = np.histogram(flat, bins=256,
                                     range=(self._vmin, self._vmax))
        self._bins  = counts.astype(np.float32)
        self._edges = edges
        self.update()

    def refresh(self) -> None:
        """Re-read window from wherever it was set externally."""
        self.update()

    def set_window(self, lo: float, hi: float) -> None:
        self._lo, self._hi = lo, hi
        self.update()

    # ── Painting ───────────────────────────────────────────────────────────────

    def paintEvent(self, _event) -> None:
        p = QPainter(self)
        p.fillRect(self.rect(), QColor(20, 20, 20))

        if self._bins is None:
            p.setPen(QColor(70, 70, 70))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "No image")
            return

        W, H = self.width(), self.height()
        margin = 4
        hist_h = H - margin * 2

        # Draw histogram bars
        mx = float(self._bins.max()) if self._bins.max() > 0 else 1.
        n  = len(self._bins)
        bar_w = max(1., (W - margin*2) / n)
        for i, cnt in enumerate(self._bins):
            bh = int(cnt / mx * hist_h)
            x  = margin + i * bar_w
            p.fillRect(int(x), H - margin - bh, max(1, int(bar_w)), bh,
                       QColor(80, 130, 200, 180))

        # W/L bar positions
        lo_x = self._val_to_px(self._lo)
        hi_x = self._val_to_px(self._hi)

        # Shaded region between bars
        grad = QLinearGradient(lo_x, 0, hi_x, 0)
        grad.setColorAt(0., QColor(255, 255, 255, 0))
        grad.setColorAt(1., QColor(255, 255, 255, 40))
        p.fillRect(QRectF(lo_x, margin, hi_x - lo_x, hist_h), grad)

        # Drag bars
        for x, color in [(lo_x, QColor(80, 200, 255)),
                         (hi_x, QColor(255, 160, 60))]:
            p.setPen(QPen(color, 2))
            p.drawLine(int(x), margin, int(x), H - margin)
            p.fillRect(int(x) - self._BAR_W // 2, H // 2 - 6,
                       self._BAR_W, 12, color)

        # Text labels
        p.setPen(QColor(200, 200, 200))
        f = p.font(); f.setPointSize(7); p.setFont(f)
        p.drawText(QPoint(int(lo_x) + 2, H - margin - 2),
                   f"{self._lo:.0f}")
        p.drawText(QPoint(int(hi_x) + 2, margin + 10),
                   f"{self._hi:.0f}")

    # ── Mouse drag ─────────────────────────────────────────────────────────────

    def mousePressEvent(self, e) -> None:
        if e.button() != Qt.MouseButton.LeftButton:
            return
        px = e.position().x()
        lo_x = self._val_to_px(self._lo)
        hi_x = self._val_to_px(self._hi)
        if abs(px - lo_x) <= self._BAR_W * 2:
            self._dragging = 'lo'
        elif abs(px - hi_x) <= self._BAR_W * 2:
            self._dragging = 'hi'

    def mouseMoveEvent(self, e) -> None:
        if not self._dragging:
            return
        val = self._px_to_val(e.position().x())
        if self._dragging == 'lo':
            self._lo = min(val, self._hi - 1.)
        else:
            self._hi = max(val, self._lo + 1.)
        self.update()
        self.windowChanged.emit(self._lo, self._hi)

    def mouseReleaseEvent(self, _e) -> None:
        self._dragging = None

    # ── Helpers ────────────────────────────────────────────────────────────────

    def _val_to_px(self, val: float) -> float:
        margin = 4
        span = max(self._vmax - self._vmin, 1e-6)
        return margin + (val - self._vmin) / span * (self.width() - margin * 2)

    def _px_to_val(self, px: float) -> float:
        margin = 4
        span = max(self._vmax - self._vmin, 1e-6)
        return self._vmin + (px - margin) / (self.width() - margin * 2) * span
