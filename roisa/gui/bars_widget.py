"""
bars_widget.py — Minimal painter-based bar/histogram plot (read-only).
"""

from __future__ import annotations

from typing import Sequence

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QFont, QPainter
from PyQt6.QtWidgets import QWidget


class BarsWidget(QWidget):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._counts: list[float] = []
        self._vmin = 0.0
        self._vmax = 1.0
        self._title = "ROI histogram"
        self.setMinimumHeight(120)
        self.setStyleSheet("background:#141414;")

    def set_data(self, counts: Sequence[float], vmin: float, vmax: float,
                 title: str = "ROI histogram") -> None:
        self._counts = list(counts)
        self._vmin, self._vmax, self._title = vmin, vmax, title
        self.update()

    def clear(self) -> None:
        self._counts = []
        self.update()

    def paintEvent(self, _e) -> None:
        p = QPainter(self)
        p.fillRect(self.rect(), QColor(20, 20, 20))
        f = QFont(); f.setPointSize(8); p.setFont(f)
        w, h = self.width(), self.height()
        ml, mr, mt, mb = 8, 8, 16, 16
        pw, ph = max(1, w - ml - mr), max(1, h - mt - mb)

        p.setPen(QColor(150, 150, 150))
        p.drawText(ml, 12, self._title)

        n = len(self._counts)
        if n == 0:
            p.setPen(QColor(110, 110, 110))
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter,
                       "Select a label, then Compute")
            return

        cmax = max(self._counts) or 1.0
        bw = pw / n
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(120, 200, 255))
        for i, c in enumerate(self._counts):
            bh = ph * (c / cmax)
            x = ml + i * bw
            p.drawRect(int(x), int(mt + ph - bh), max(1, int(bw) - 1), int(bh))

        # min / max labels
        p.setPen(QColor(140, 140, 140))
        p.drawText(ml, h - 3, f"{self._vmin:.1f}")
        p.drawText(w - mr - 40, h - 3, f"{self._vmax:.1f}")
