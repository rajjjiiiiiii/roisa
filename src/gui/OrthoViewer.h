#pragma once
// OrthoViewer.h — Multi-panel orthogonal viewer with switchable layout presets.
//
//  Preset 0  "2×2"   VTK | Sag / Cor | Axi   (equal quadrants, default)
//  Preset 1  "1+3"   large Axial | Sag/Cor/VTK stacked
//  Preset 2  "3-up"  Sag | Cor | Axi side-by-side (VTK hidden)
//  Preset 3  "Axi"   Axial only
//  Preset 4  "3D"    VTK only

#include <QPixmap>
#include <QWidget>
#include "../core/ROIVolume.h"

#include "SliceView.h"

class VtkView;
class QTimer;
class QSplitter;
class QVBoxLayout;
class QPushButton;

class OrthoViewer : public QWidget
{
    Q_OBJECT

public:
    explicit OrthoViewer(QWidget* parent = nullptr);

    void setVolume(ROIVolume* vol);

    // Navigate — called by ToolPanel sliders
    void setX(int x);
    void setY(int y);
    void setZ(int z);

    int x() const { return m_x; }
    int y() const { return m_y; }
    int z() const { return m_z; }

    // Last painted/clicked voxel (used as seed for segmentation)
    int seedX() const { return m_seedX; }
    int seedY() const { return m_seedY; }
    int seedZ() const { return m_seedZ; }

    void refresh();                     // repaint all three slice views

    int activeAxis() const { return m_activeAxis; }

    // 3-D render
    void refreshSurface(int label = -1); // rebuild surface mesh(es); -1 = all
    void refreshVtk();                   // full VTK refresh (volume + surfaces)
    void setVtkRenderMode(int mode);     // 0=vol, 1=surf, 2=both
    void resetVtkCamera();

    // Panel snapshots for Export (ToolPanel grabs individual views as PNG/movie)
    QPixmap grabSagittal() const;
    QPixmap grabCoronal()  const;
    QPixmap grabAxial()    const;
    QPixmap grabVtk()      const;

    // No-op kept for API compatibility (histogram now lives in ToolPanel)
    void refreshHistogram() {}

    // Measurement overlay controls (0=None,1=Ruler,2=Angle,3=Circle)
    void setMeasureMode(int mode);
    void clearMeasurements();

    // Display option passthroughs to slice views
    void setInterpolate(bool on);
    void setColormap(int cm);
    void setOverlayAlpha(float a);
    void setAllLabelsVisible(bool v);
    void setShowInfoOverlay(bool on);
    void setProjectionMode(int mode);
    void setSlab(int slab);
    void setShowColorbar(bool on);
    void setVtkClip(bool enabled, int axis, double frac);
    void resetAllZoom();

    // ── Fusion passthroughs ────────────────────────────────────────────────────
    void setOverlays(const std::vector<SliceView::FusionLayer>& ov);
    void setBaseVisible(bool on);
    void setBaseColormap(int cm);
    int  baseColormap() const;

    // ── Cine / loop player ────────────────────────────────────────────────────
    void playCine();
    void stopCine();
    void setCineAxis(int axis);   // 0=Sag, 1=Cor, 2=Axi
    void setCineFps(int fps);     // 1–30
    bool cinePlaying() const { return m_cinePlaying; }

    // ── Layout presets ────────────────────────────────────────────────────────
    // 0=2×2  1=1+3(Axi large)  2=3-up(slices)  3=Axi only  4=3D only
    void setLayoutPreset(int preset);
    int  layoutPreset()  const { return m_currentPreset; }

signals:
    void seedSet(int x, int y, int z);
    void positionChanged(int x, int y, int z);
    void sliceReleased();
    void measurementAdded(QString description);

private slots:
    void onSliceClicked (int x, int y, int z);
    void onSliceDragged (int x, int y, int z);
    void onSliceScrolled(int viewerAxis, int delta);
    void onCineTick();

private:
    ROIVolume* m_vol{nullptr};

    VtkView*   m_vtkView;   // (0,0) top-left:     3-D VTK render
    SliceView* m_sagView;   // (0,1) top-right:    sagittal  (axis 0)
    SliceView* m_corView;   // (1,0) bottom-left:  coronal   (axis 1)
    SliceView* m_axiView;   // (1,1) bottom-right: transverse (axis 2)

    int m_x{0}, m_y{0}, m_z{0};
    int m_seedX{0}, m_seedY{0}, m_seedZ{0};
    int m_activeAxis{2};

    // Cine state
    QTimer* m_cineTimer{nullptr};
    int     m_cineAxis{2};
    int     m_cineFps{8};
    bool    m_cinePlaying{false};

    // Layout preset state
    QWidget*     m_viewContainer{nullptr};
    QVBoxLayout* m_viewContainerLayout{nullptr};
    QSplitter*   m_currentSplitter{nullptr};
    int          m_currentPreset{0};
    QPushButton* m_presetBtns[5]{};

    void applyLayoutPreset(int preset);
    void updateCrosshairs();
};
