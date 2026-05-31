"""
image_list_widget.py — Compact panel listing all loaded images with
eye-toggle visibility, REF/IN badges, click-to-activate, and remove buttons.

Signals
-------
addRequested        ()              user clicked "+ Add"
activateRequested   (int)           user clicked a row name → make it active
visibilityToggled   (int, bool)     user clicked the eye toggle
removeRequested     (int)           user clicked ✕
"""

from __future__ import annotations

import os
from typing import Optional

from PyQt6.QtCore    import Qt, pyqtSignal
from PyQt6.QtWidgets import (
    QFrame, QHBoxLayout, QLabel, QPushButton, QSizePolicy,
    QVBoxLayout, QWidget,
)


# ── Style constants ───────────────────────────────────────────────────────────

_EYE_ON  = "●"
_EYE_OFF = "○"
_REMOVE  = "✕"

def _eye_style(on: bool) -> str:
    colour = "#6af" if on else "#445"
    return (f"QPushButton{{color:{colour};background:transparent;"
            f"border:none;font-size:11px;}}")

_REF_BADGE_STYLE = (
    "QLabel{background:#1a5fb4;color:#fff;border-radius:3px;"
    "padding:1px 3px;font-size:8px;font-weight:bold;}")

_IN_BADGE_STYLE = (
    "QLabel{background:#c06000;color:#fff;border-radius:3px;"
    "padding:1px 3px;font-size:8px;font-weight:bold;}")

def _name_style(active: bool) -> str:
    colour = "#eee" if active else "#aaa"
    hover  = "#fff" if active else "#ddd"
    return (f"QPushButton{{color:{colour};background:transparent;border:none;"
            f"font-size:10px;text-align:left;padding:0 2px;}}"
            f"QPushButton:hover{{color:{hover};}}")

_REMOVE_STYLE = (
    "QPushButton{color:#844;background:transparent;border:none;font-size:9px;}"
    "QPushButton:hover{color:#f66;}"
    "QPushButton:disabled{color:#2a2a2a;}")

def _row_style(active: bool, enabled: bool) -> str:
    if not enabled:
        return "QWidget{background:#111;border-radius:3px;}"
    if active:
        return "QWidget{background:#1a3d5c;border-radius:3px;}"
    return "QWidget{background:transparent;}"


# ── Row dataclass ─────────────────────────────────────────────────────────────

class _Row:
    __slots__ = ("widget", "eye_btn", "badge", "name_btn", "remove_btn", "enabled")

    def __init__(self, widget: QWidget, eye_btn: QPushButton,
                 badge: QLabel, name_btn: QPushButton,
                 remove_btn: QPushButton) -> None:
        self.widget     = widget
        self.eye_btn    = eye_btn
        self.badge      = badge
        self.name_btn   = name_btn
        self.remove_btn = remove_btn
        self.enabled    = True


# ═════════════════════════════════════════════════════════════════════════════

class ImageListWidget(QWidget):
    addRequested       = pyqtSignal()
    activateRequested  = pyqtSignal(int)
    visibilityToggled  = pyqtSignal(int, bool)
    removeRequested    = pyqtSignal(int)

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._rows:       list[_Row] = []
        self._active_idx: int        = -1

        outer = QVBoxLayout(self)
        outer.setContentsMargins(4, 4, 4, 2)
        outer.setSpacing(3)

        # ── Header: "Images" + "+ Add" ────────────────────────────────────────
        hdr  = QWidget(self)
        hdrL = QHBoxLayout(hdr)
        hdrL.setContentsMargins(0, 0, 0, 0)
        hdrL.setSpacing(4)

        title_lbl = QLabel("Images", hdr)
        title_lbl.setStyleSheet(
            "color:#9a9; font-size:10px; font-weight:bold;")
        hdrL.addWidget(title_lbl)
        hdrL.addStretch()

        add_btn = QPushButton("＋ Add", hdr)
        add_btn.setFixedHeight(18)
        add_btn.setStyleSheet(
            "QPushButton{background:#1a3d1a;color:#8c8;border:1px solid #3a6a3a;"
            "border-radius:2px;padding:1px 8px;font-size:10px;}"
            "QPushButton:hover{background:#246024;color:#afa;}")
        add_btn.clicked.connect(self.addRequested)
        hdrL.addWidget(add_btn)
        outer.addWidget(hdr)

        # ── Separator ─────────────────────────────────────────────────────────
        sep = QFrame(self)
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet("border:none;background:#2a2a2a;max-height:1px;")
        outer.addWidget(sep)

        # ── Row container ─────────────────────────────────────────────────────
        self._list_container = QWidget(self)
        self._list_container.setStyleSheet(
            "QWidget{background:#181818;border:1px solid #2a2a2a;"
            "border-radius:3px;}")
        self._list_layout = QVBoxLayout(self._list_container)
        self._list_layout.setContentsMargins(2, 2, 2, 2)
        self._list_layout.setSpacing(1)
        outer.addWidget(self._list_container)

        self._show_empty_placeholder()

    # ── Placeholder ───────────────────────────────────────────────────────────

    def _show_empty_placeholder(self) -> None:
        lbl = QLabel("No images loaded — use File → Open", self)
        lbl.setStyleSheet("color:#444; font-size:9px; padding:6px 4px;")
        lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lbl.setObjectName("_placeholder")
        self._list_layout.addWidget(lbl)

    def _remove_placeholder(self) -> None:
        for i in range(self._list_layout.count()):
            item = self._list_layout.itemAt(i)
            if item and item.widget() and \
                    item.widget().objectName() == "_placeholder":
                w = self._list_layout.takeAt(i).widget()
                w.deleteLater()
                return

    # ── Public API ────────────────────────────────────────────────────────────

    def clear(self) -> None:
        """Remove all rows and show the placeholder."""
        self._rows.clear()
        self._active_idx = -1
        while self._list_layout.count():
            item = self._list_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
            del item
        self._show_empty_placeholder()

    def add_image(self, display_name: str, is_ref: bool = False) -> None:
        """Append one row. is_ref=True → blue REF badge; else orange INn badge."""
        if len(self._rows) == 0:
            self._remove_placeholder()

        idx = len(self._rows)

        row = QWidget(self)
        row_layout = QHBoxLayout(row)
        row_layout.setContentsMargins(3, 1, 3, 1)
        row_layout.setSpacing(3)

        # Eye toggle
        eye_btn = QPushButton(_EYE_ON, row)
        eye_btn.setFixedSize(16, 16)
        eye_btn.setFlat(True)
        eye_btn.setStyleSheet(_eye_style(True))
        eye_btn.setToolTip("Toggle visibility")
        row_layout.addWidget(eye_btn)

        # Badge
        badge_text = "REF" if is_ref else f"IN{idx}"
        badge = QLabel(badge_text, row)
        badge.setFixedWidth(28)
        badge.setAlignment(Qt.AlignmentFlag.AlignCenter)
        badge.setStyleSheet(_REF_BADGE_STYLE if is_ref else _IN_BADGE_STYLE)
        row_layout.addWidget(badge)

        # Name button (flat, click → activate)
        short = os.path.basename(display_name) or display_name
        if len(short) > 22:
            short = short[:10] + "…" + short[-9:]
        name_btn = QPushButton(short, row)
        name_btn.setFlat(True)
        name_btn.setToolTip(display_name)
        name_btn.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        name_btn.setStyleSheet(_name_style(False))
        row_layout.addWidget(name_btn, 1)

        # Remove button
        rm_btn = QPushButton(_REMOVE, row)
        rm_btn.setFixedSize(16, 16)
        rm_btn.setFlat(True)
        rm_btn.setStyleSheet(_REMOVE_STYLE)
        rm_btn.setToolTip(
            "Cannot remove reference while inputs exist" if is_ref
            else "Remove this input image")
        row_layout.addWidget(rm_btn)

        # Wire signals (capture idx by closure)
        def _eye_clicked(checked=False, _i=idx):
            self.visibilityToggled.emit(_i, not self._rows[_i].enabled)
        def _name_clicked(checked=False, _i=idx):
            self.activateRequested.emit(_i)
        def _rm_clicked(checked=False, _i=idx):
            self.removeRequested.emit(_i)

        eye_btn.clicked.connect(_eye_clicked)
        name_btn.clicked.connect(_name_clicked)
        rm_btn.clicked.connect(_rm_clicked)

        r = _Row(row, eye_btn, badge, name_btn, rm_btn)
        self._rows.append(r)
        self._list_layout.addWidget(row)

    def set_active(self, idx: int) -> None:
        """Highlight a row as the currently viewed image."""
        prev = self._active_idx
        self._active_idx = idx
        if 0 <= prev < len(self._rows):
            self._apply_row_style(prev)
        if 0 <= idx < len(self._rows):
            self._apply_row_style(idx)

    def set_enabled(self, idx: int, on: bool) -> None:
        """Update the eye glyph and dim/undim a row."""
        if idx < 0 or idx >= len(self._rows):
            return
        row = self._rows[idx]
        row.enabled = on
        row.eye_btn.setText(_EYE_ON if on else _EYE_OFF)
        row.eye_btn.setStyleSheet(_eye_style(on))
        self._apply_row_style(idx)

    def set_remove_enabled(self, idx: int, on: bool) -> None:
        """Enable or disable the ✕ button for a row."""
        if 0 <= idx < len(self._rows):
            self._rows[idx].remove_btn.setEnabled(on)

    # ── Private ───────────────────────────────────────────────────────────────

    def _apply_row_style(self, idx: int) -> None:
        if idx < 0 or idx >= len(self._rows):
            return
        row    = self._rows[idx]
        active = (idx == self._active_idx)
        row.widget.setStyleSheet(_row_style(active, row.enabled))
        row.name_btn.setStyleSheet(_name_style(active))
