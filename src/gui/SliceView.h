#pragma once
// SliceView.h — Single orthogonal slice panel with mask overlay and mouse input

#include <QWidget>
#include <array>
#include <cstdint>
#include <vector>

#include "../core/ROIVolume.h"

class SliceView : public QWidget
{
    Q_OBJECT

public:
    // axis: 0 = sagittal (x = const)
    //       1 = coronal  (y = const)
    //       2 = axial    (z = const)
    explicit SliceView(int axis, QWidget* parent = nullptr);

    void setVolume(ROIVolume* vol);
    void setSliceIndex(int idx);
    int  sliceIndex() const { return m_sliceIdx; }
    int  axis()       const { return m_axis; }

    // Crosshair coords in the other two dimensions:
    //   axis 0 (sag): a = y, b = z
    //   axis 1 (cor): a = x, b = z
    //   axis 2 (axi): a = x, b = y
    void setCrosshair(int a, int b);

signals:
    void sliceClicked (int x, int y, int z);
    void sliceDragged (int x, int y, int z);
    void sliceReleased();
    void scrolled(int delta);   // mouse wheel step

protected:
    void paintEvent       (QPaintEvent*)  override;
    void mousePressEvent  (QMouseEvent*)  override;
    void mouseMoveEvent   (QMouseEvent*)  override;
    void mouseReleaseEvent(QMouseEvent*)  override;
    void wheelEvent       (QWheelEvent*)  override;

private:
    ROIVolume* m_vol{nullptr};
    int        m_axis;
    int        m_sliceIdx{0};
    int        m_crossA{0}, m_crossB{0};
    bool       m_dragging{false};

    mutable std::vector<float>   m_intensityBuf;
    mutable std::vector<int16_t> m_maskBuf;

    // Return the letterboxed image rect within this widget
    QRect imageRect() const;

    // Map widget pixel (wx,wy) → display-space (vx,vy,vz); false if outside
    bool widgetToVoxel(int wx, int wy, int& vx, int& vy, int& vz) const;

    // Cycled RGBA colour for label index (matches Python _make_label_colors)
    static std::array<uint8_t,4> labelColor(int label);
};
