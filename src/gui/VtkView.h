#pragma once
// VtkView.h — VTK 3-D render panel (volume + label surfaces)
//
// When ROISA_USE_VTK is defined the widget hosts a QVTKOpenGLNativeWidget that
// drives a vtkSmartVolumeMapper (volume) and per-label marching-cube surfaces.
// When VTK is absent a styled placeholder label is shown instead — the rest of
// ROISA compiles and runs normally.

#include <QWidget>
#include <cstdint>
#include <vector>

class ROIVolume;

#ifdef ROISA_USE_VTK
class QVTKOpenGLNativeWidget;
#  include <vtkSmartPointer.h>
class vtkRenderer;
class vtkGenericOpenGLRenderWindow;
class vtkSmartVolumeMapper;
class vtkVolume;
class vtkColorTransferFunction;
class vtkPiecewiseFunction;
class vtkVolumeProperty;
class vtkImageData;
class vtkActor;
class vtkPolyDataMapper;
class vtkPlane;
class vtkOrientationMarkerWidget;
#endif

class VtkView : public QWidget
{
    Q_OBJECT
public:
    explicit VtkView(QWidget* parent = nullptr);
    ~VtkView() override;

    void setVolume(ROIVolume* vol);

    /// Rebuild VTK image data from ITK buffer + refresh transfer functions.
    void refreshVolume();

    /// Rebuild surface mesh(es).  label = -1 rebuilds all active labels.
    void refreshSurface(int label = -1);

    void resetCamera();

    /// Render mode: 0 = volume only, 1 = surfaces only, 2 = both (default).
    void setRenderMode(int mode);
    int  renderMode() const { return m_renderMode; }

    /// Clip plane along axis (0=X,1=Y,2=Z) at fractional position frac in [0,1].
    void setClip(bool enabled, int axis, double frac);

    /// Returns true when compiled with VTK support.
    static bool isAvailable();

private:
    ROIVolume* m_vol      {nullptr};
    int        m_renderMode{2};
    int        m_surfLabel {-1};
    bool       m_clipOn    {false};
    int        m_clipAxis  {2};
    double     m_clipFrac  {0.5};

#ifdef ROISA_USE_VTK
    QVTKOpenGLNativeWidget*                       m_vtkWidget {nullptr};
    vtkSmartPointer<vtkRenderer>                  m_renderer;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renWin;
    vtkSmartPointer<vtkImageData>                 m_vtkImage;
    vtkSmartPointer<vtkSmartVolumeMapper>         m_volMapper;
    vtkSmartPointer<vtkVolume>                    m_volActor;
    vtkSmartPointer<vtkColorTransferFunction>     m_colorTfn;
    vtkSmartPointer<vtkPiecewiseFunction>         m_opacityTfn;
    vtkSmartPointer<vtkVolumeProperty>            m_volProp;

    struct LabelSurface {
        int   label{0};
        vtkSmartPointer<vtkActor>          actor;
        vtkSmartPointer<vtkPolyDataMapper> mapper;
    };
    std::vector<LabelSurface> m_surfaces;
    vtkSmartPointer<vtkPlane>                     m_clipPlane;
    vtkSmartPointer<vtkOrientationMarkerWidget>   m_orientWidget;

    void applyClip();
    void initVtk();
    void copyItkToVtk();
    void rebuildSurfaces();
    void applyRenderMode();
#endif
};
