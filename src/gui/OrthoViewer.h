#pragma once
// OrthoViewer.h — Three-panel orthogonal viewer with linked crosshairs

#include <QWidget>
#include "../core/ROIVolume.h"

class SliceView;

class OrthoViewer : public QWidget
{
    Q_OBJECT

public:
    explicit OrthoViewer(QWidget* parent = nullptr);

    void setVolume(ROIVolume* vol);

    // Navigate to (x, y, z) — called by ToolPanel sliders
    void setX(int x);
    void setY(int y);
    void setZ(int z);

    int x() const { return m_x; }
    int y() const { return m_y; }
    int z() const { return m_z; }

    // Last position where the user clicked (used as seed)
    int seedX() const { return m_seedX; }
    int seedY() const { return m_seedY; }
    int seedZ() const { return m_seedZ; }

    void refresh();

    // Axis (0=sagittal,1=coronal,2=axial) of the panel last clicked/dragged
    int activeAxis() const { return m_activeAxis; }

signals:
    void seedSet(int x, int y, int z);
    void positionChanged(int x, int y, int z);
    void sliceReleased();   // forwarded from whichever panel the user released in

private slots:
    void onSliceClicked (int x, int y, int z);
    void onSliceDragged (int x, int y, int z);
    void onSliceScrolled(int viewerAxis, int delta);

private:
    ROIVolume* m_vol{nullptr};

    SliceView* m_sagView;   // axis 0
    SliceView* m_corView;   // axis 1
    SliceView* m_axiView;   // axis 2

    int m_x{0}, m_y{0}, m_z{0};
    int m_seedX{0}, m_seedY{0}, m_seedZ{0};
    int m_activeAxis{2};   // axis of last-active panel (default axial)

    void updateCrosshairs();
};
