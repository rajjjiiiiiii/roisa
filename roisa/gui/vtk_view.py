"""
vtk_view.py — VTK 3-D rendering panel (volume + surface).

Uses vtkmodules with PyQt6 via QVTKRenderWindowInteractor.
"""

from __future__ import annotations

from typing import Optional

import numpy as np
from PyQt6.QtWidgets import QVBoxLayout, QWidget

# Tell vtkmodules to use PyQt6
try:
    import vtkmodules.qt
    vtkmodules.qt.PyQtImpl = 'PyQt6'
    from vtkmodules.qt.QVTKRenderWindowInteractor import QVTKRenderWindowInteractor
    import vtkmodules.all as vtk
    _VTK_OK = True
except Exception as _e:
    print(f"[VtkView] VTK not available: {_e}")
    _VTK_OK = False


class VtkView(QWidget):
    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._vol        = None
        self._render_mode = 2   # 0=volume 1=surfaces 2=both
        self._vtk_ok      = _VTK_OK

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        if not _VTK_OK:
            from PyQt6.QtWidgets import QLabel
            lbl = QLabel("VTK not available.\npip install vtk", self)
            from PyQt6.QtCore import Qt
            lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lbl.setStyleSheet("color:#666; font-size:11px;")
            layout.addWidget(lbl)
            return

        self._vtkWidget = QVTKRenderWindowInteractor(self)
        layout.addWidget(self._vtkWidget)

        self._renderer = vtk.vtkRenderer()
        self._renderer.SetBackground(.1, .1, .1)
        self._vtkWidget.GetRenderWindow().AddRenderer(self._renderer)

        style = vtk.vtkInteractorStyleTrackballCamera()
        self._vtkWidget.GetRenderWindow().GetInteractor().SetInteractorStyle(style)

        # Actors (created lazily)
        self._vol_actor:  Optional[vtk.vtkVolume]         = None
        self._surf_actor: Optional[vtk.vtkActor]          = None

    def showEvent(self, event) -> None:
        super().showEvent(event)
        if _VTK_OK:
            self._vtkWidget.Initialize()

    # ── Public API ─────────────────────────────────────────────────────────────

    def setVolume(self, vol) -> None:
        self._vol = vol
        if not _VTK_OK or vol is None or not vol.is_loaded():
            return
        self.refreshVolume()
        self.refreshSurface(-1)

    def setRenderMode(self, mode: int) -> None:
        self._render_mode = mode
        self._apply_render_mode()

    def renderMode(self) -> int:
        return self._render_mode

    def refreshVolume(self) -> None:
        if not _VTK_OK or not self._vol or not self._vol.is_loaded():
            return
        try:
            self._build_volume_actor()
            self._apply_render_mode()
            self._renderer.ResetCamera()
            self._vtkWidget.GetRenderWindow().Render()
        except Exception as e:
            print(f"[VtkView.refreshVolume] {e}")

    def refreshSurface(self, label: int = -1) -> None:
        if not _VTK_OK or not self._vol or not self._vol.is_loaded():
            return
        try:
            self._build_surface_actor(label)
            self._apply_render_mode()
            self._vtkWidget.GetRenderWindow().Render()
        except Exception as e:
            print(f"[VtkView.refreshSurface] {e}")

    def resetCamera(self) -> None:
        if not _VTK_OK:
            return
        self._renderer.ResetCamera()
        self._vtkWidget.GetRenderWindow().Render()

    # ── VTK pipeline builders ──────────────────────────────────────────────────

    def _build_volume_actor(self) -> None:
        if self._vol_actor:
            self._renderer.RemoveVolume(self._vol_actor)
            self._vol_actor = None

        arr = self._vol.arr.astype(np.float32)
        sp  = self._vol.spacing_xyz()   # (sx, sy, sz)

        # Reshape to (x, y, z) for VTK
        arr_vtk = np.ascontiguousarray(arr.transpose(2, 1, 0))

        importer = vtk.vtkImageImport()
        flat = arr_vtk.ravel()
        importer.CopyImportVoidPointer(flat, flat.nbytes)
        importer.SetDataScalarTypeToFloat()
        importer.SetNumberOfScalarComponents(1)
        nx, ny, nz = arr_vtk.shape
        importer.SetDataExtent(0, nx-1, 0, ny-1, 0, nz-1)
        importer.SetWholeExtent(0, nx-1, 0, ny-1, 0, nz-1)
        importer.SetDataSpacing(sp[0], sp[1], sp[2])
        importer.Update()

        # Transfer functions
        lo, hi = self._vol.vmin(), self._vol.vmax()
        mid = (lo + hi) * .5

        opacity_tf = vtk.vtkPiecewiseFunction()
        opacity_tf.AddPoint(lo,  0.0)
        opacity_tf.AddPoint(mid, 0.05)
        opacity_tf.AddPoint(hi,  0.2)

        color_tf = vtk.vtkColorTransferFunction()
        color_tf.AddRGBPoint(lo,  0.0, 0.0, 0.0)
        color_tf.AddRGBPoint(mid, 0.6, 0.6, 0.6)
        color_tf.AddRGBPoint(hi,  1.0, 1.0, 1.0)

        prop = vtk.vtkVolumeProperty()
        prop.SetColor(color_tf)
        prop.SetScalarOpacity(opacity_tf)
        prop.SetInterpolationTypeToLinear()
        prop.ShadeOn()

        mapper = vtk.vtkSmartVolumeMapper()
        mapper.SetInputConnection(importer.GetOutputPort())

        self._vol_actor = vtk.vtkVolume()
        self._vol_actor.SetMapper(mapper)
        self._vol_actor.SetProperty(prop)
        self._renderer.AddVolume(self._vol_actor)

    def _build_surface_actor(self, label: int) -> None:
        if self._surf_actor:
            self._renderer.RemoveActor(self._surf_actor)
            self._surf_actor = None

        mask = self._vol.mask
        if mask is None:
            return
        sp = self._vol.spacing_xyz()

        # Build a binary array for the surface
        if label == -1:
            binary = (mask > 0).astype(np.uint8)
        elif label == -2:
            return  # signal: don't rebuild
        else:
            binary = (mask == label).astype(np.uint8)

        if not binary.any():
            return

        # (nz,ny,nx) → (nx,ny,nz) for VTK
        arr_vtk = np.ascontiguousarray(binary.transpose(2, 1, 0))
        nx, ny, nz = arr_vtk.shape

        importer = vtk.vtkImageImport()
        flat = arr_vtk.ravel()
        importer.CopyImportVoidPointer(flat, flat.nbytes)
        importer.SetDataScalarTypeToUnsignedChar()
        importer.SetNumberOfScalarComponents(1)
        importer.SetDataExtent(0, nx-1, 0, ny-1, 0, nz-1)
        importer.SetWholeExtent(0, nx-1, 0, ny-1, 0, nz-1)
        importer.SetDataSpacing(sp[0], sp[1], sp[2])
        importer.Update()

        mc = vtk.vtkMarchingCubes()
        mc.SetInputConnection(importer.GetOutputPort())
        mc.SetValue(0, .5)
        mc.Update()

        smooth = vtk.vtkSmoothPolyDataFilter()
        smooth.SetInputConnection(mc.GetOutputPort())
        smooth.SetNumberOfIterations(20)
        smooth.Update()

        normals = vtk.vtkPolyDataNormals()
        normals.SetInputConnection(smooth.GetOutputPort())

        mapper = vtk.vtkPolyDataMapper()
        mapper.SetInputConnection(normals.GetOutputPort())
        mapper.ScalarVisibilityOff()

        self._surf_actor = vtk.vtkActor()
        self._surf_actor.SetMapper(mapper)
        self._surf_actor.GetProperty().SetColor(.8, .3, .2)
        self._surf_actor.GetProperty().SetOpacity(.7)
        self._renderer.AddActor(self._surf_actor)

    def _apply_render_mode(self) -> None:
        if not _VTK_OK:
            return
        show_vol  = self._render_mode in (0, 2)
        show_surf = self._render_mode in (1, 2)
        if self._vol_actor:
            self._vol_actor.SetVisibility(show_vol)
        if self._surf_actor:
            self._surf_actor.SetVisibility(show_surf)
