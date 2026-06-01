"""
settings_dialog.py — Preferences dialog (default brush, colormap, isotope).

Values are persisted in QSettings and returned to the caller, which applies
them to the tool panel.
"""

from __future__ import annotations

from PyQt6.QtCore import QSettings
from PyQt6.QtWidgets import (
    QComboBox, QDialog, QDialogButtonBox, QFormLayout, QSpinBox,
)

# Common PET isotopes and their half-lives (seconds)
ISOTOPES = [
    ("F-18  (6586 s)",  6586.2),
    ("C-11  (1223 s)",  1223.4),
    ("N-13  (598 s)",    597.9),
    ("O-15  (122 s)",    122.24),
    ("Ga-68 (4062 s)",  4062.0),
    ("Cu-64 (45720 s)", 45720.0),
    ("Zr-89 (282240 s)",282240.0),
]


class SettingsDialog(QDialog):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Preferences")
        self._s = QSettings("ROISA", "ROISA")
        form = QFormLayout(self)

        self._brush = QSpinBox(); self._brush.setRange(1, 50)
        self._brush.setValue(int(self._s.value("brushRadius", 3)))
        form.addRow("Default brush radius", self._brush)

        self._cmap = QComboBox(); self._cmap.addItems(["Gray", "Hot", "Cool", "Viridis"])
        self._cmap.setCurrentIndex(int(self._s.value("colormap", 0)))
        form.addRow("Default colormap", self._cmap)

        self._iso = QComboBox()
        for name, _ in ISOTOPES:
            self._iso.addItem(name)
        self._iso.setCurrentIndex(int(self._s.value("isotope", 0)))
        form.addRow("PET isotope", self._iso)

        bb = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        bb.accepted.connect(self._accept)
        bb.rejected.connect(self.reject)
        form.addRow(bb)

    def _accept(self) -> None:
        self._s.setValue("brushRadius", self._brush.value())
        self._s.setValue("colormap",    self._cmap.currentIndex())
        self._s.setValue("isotope",     self._iso.currentIndex())
        self.accept()

    def values(self) -> dict:
        return {
            "brushRadius": self._brush.value(),
            "colormap":    self._cmap.currentIndex(),
            "halfLifeS":   ISOTOPES[self._iso.currentIndex()][1],
        }
