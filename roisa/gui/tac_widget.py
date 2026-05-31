"""
tac_widget.py — Small painter-based time-activity-curve / bar plot.

Plots a list of float values (one per frame) as a line+marker chart with
simple axes.  No external charting dependency.
"""

from __future__ import annotations

import math
from typing import List

from PyQt6.QtCore import Qt, QPointF
from PyQt6.QtGui import QColor, QFont, QPainter, QPen, QPolygonF
from PyQt6.QtWidgets import QWidget


class TacWidget(QWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._values: List[float] = []
        self._title = "Time-Activity Curve"
        self._ylabel = "SUVmean"
        self.setMinimumHeight(140)
        self.setStyleSheet("background:#141414;")

    def set_values(self, values: List[float], ylabel: str = "SUVmean") -> None:
        self._values = [v for v in values if v is not None and not math.isnan(v)]
        self._ylabel = ylabel
        self.update()

    def clear(self) -> None:
        self._values = []
        self.update()

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.fillRect(self.rect(), QColor(20, 20, 20))
        f = QFont(); f.setPointSize(8); p.setFont(f)

        w, h = self.width(), self.height()
        ml, mr, mt, mb = 40, 10, 18, 22          # margins
        x0, y0 = ml, mt
        pw, ph = max(1, w - ml - mr), max(1, h - mt - mb)

        # Axes
        p.setPen(QPen(QColor(90, 90, 90), 1))
        p.drawLine(x0, y0, x0, y0 + ph)
        p.drawLine(x0, y0 + ph, x0 + pw, y0 + ph)

        p.setPen(QColor(150, 150, 150))
        p.drawText(x0, 12, f"{self._title}   ({self._ylabel})")

        n = len(self._values)
        if n == 0:
            p.setPen(QColor(110, 110, 110))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                       "No curve — draw an ROI, load frames, Compute")
            return

        vmax = max(self._values); vmin = min(self._values)
        if vmax - vmin < 1e-9:
            vmax = vmin + 1.0
        rng = vmax - vmin

        # y gridlines + labels (min / max)
        p.setPen(QColor(120, 120, 120))
        p.drawText(2, y0 + 6, f"{vmax:.2f}")
        p.drawText(2, y0 + ph, f"{vmin:.2f}")

        def pt(i: int) -> QPointF:
            x = x0 + (pw * (i / (n - 1))) if n > 1 else x0 + pw / 2
            y = y0 + ph - ph * ((self._values[i] - vmin) / rng)
            return QPointF(x, y)

        # Line
        poly = QPolygonF([pt(i) for i in range(n)])
        p.setPen(QPen(QColor(120, 200, 255), 2))
        p.drawPolyline(poly)

        # Markers + value labels
        p.setPen(QPen(QColor(255, 210, 90), 1))
        for i in range(n):
            q = pt(i)
            p.setBrush(QColor(255, 210, 90))
            p.drawEllipse(q, 3, 3)
            p.setPen(QColor(170, 170, 170))
            p.drawText(int(q.x()) - 8, y0 + ph + 14, f"{i+1}")
            p.setPen(QPen(QColor(255, 210, 90), 1))
