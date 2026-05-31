#!/usr/bin/env python3
"""
ROISA — ROI Segmentation & Analysis (PyQt6 port)

Usage:
    python main.py [image_path]

    image_path  optional DICOM folder or NIfTI file to open on startup
"""

import sys
import os

# Suppress VTK/Qt warning noise
os.environ.setdefault("VTK_SILENCE_GET_VOID_POINTER_WARNINGS", "1")

from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore    import Qt
from PyQt6.QtGui     import QFont

from roisa.gui.main_window import MainWindow


def main() -> None:
    # Enable HiDPI
    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)

    app = QApplication(sys.argv)
    app.setApplicationName("ROISA")
    app.setOrganizationName("ROISA")

    # Base font — use system font via .AppleSystemUIFont on macOS to avoid
    # the "SF Pro Text missing" alias warning Qt emits.
    if sys.platform == "darwin":
        f = QFont(".AppleSystemUIFont")
    else:
        f = QFont("Segoe UI")
    f.setPointSize(11)
    app.setFont(f)

    win = MainWindow()
    win.show()

    # Open file passed on command line
    if len(sys.argv) > 1:
        path = sys.argv[1]
        if os.path.exists(path):
            win._vol.load(path)
            win._panel.setVolume(win._vol)
            win._viewer.setVolume(win._vol)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
