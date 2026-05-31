"""
dicom_tag_widget.py — Scrollable DICOM / NIfTI metadata inspector.
"""

from __future__ import annotations

from typing import Optional

from PyQt6.QtWidgets import QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget


class DicomTagWidget(QWidget):
    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._table = QTableWidget(0, 2, self)
        self._table.setHorizontalHeaderLabels(["Tag / Key", "Value"])
        self._table.horizontalHeader().setStretchLastSection(True)
        self._table.setEditTriggers(
            QTableWidget.EditTrigger.NoEditTriggers)
        self._table.setAlternatingRowColors(True)
        self._table.setStyleSheet(
            "QTableWidget{font-size:10px;}"
            "QHeaderView::section{background:#252525;}")
        l = QVBoxLayout(self)
        l.setContentsMargins(0, 0, 0, 0)
        l.addWidget(self._table)

    def setVolume(self, vol) -> None:
        self._table.setRowCount(0)
        if vol is None or not vol.is_loaded():
            return
        img = vol.sitk_img
        if img is None:
            return

        rows = []
        # Basic geometry
        rows += [
            ("Size (x,y,z)", str(img.GetSize())),
            ("Spacing (mm)",  str(tuple(f"{v:.4f}" for v in img.GetSpacing()))),
            ("Origin",        str(tuple(f"{v:.2f}" for v in img.GetOrigin()))),
            ("Pixel type",    img.GetPixelIDTypeAsString()),
        ]
        # DICOM metadata if available
        try:
            import pydicom
            from pathlib import Path
            # We don't store the path, so just try what SimpleITK has
            for k in img.GetMetaDataKeys():
                try:
                    rows.append((k, img.GetMetaData(k)))
                except Exception:
                    pass
        except ImportError:
            pass

        self._table.setRowCount(len(rows))
        for i, (k, v) in enumerate(rows):
            self._table.setItem(i, 0, QTableWidgetItem(str(k)))
            self._table.setItem(i, 1, QTableWidgetItem(str(v).strip()))
        self._table.resizeColumnToContents(0)
