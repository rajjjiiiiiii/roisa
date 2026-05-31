#pragma once
// HistogramWidget.h — Intensity histogram with W/L drag handles

#include <QWidget>
#include <vector>
#include "../core/ROIVolume.h"

class HistogramWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HistogramWidget(QWidget* parent = nullptr);

    void setVolume(ROIVolume* vol);
    void refresh();   // rebuild histogram from volume

signals:
    void windowChanged(float vmin, float vmax);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*)   override;
    void mouseMoveEvent(QMouseEvent*)    override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    ROIVolume* m_vol{nullptr};

    static constexpr int BINS = 256;
    std::vector<int>  m_counts;   // BINS counts
    float m_dataMin{0.f}, m_dataMax{1.f};

    // Drag state
    enum DragTarget { NONE, VMIN_HANDLE, VMAX_HANDLE };
    DragTarget m_drag{NONE};

    // Map intensity value to widget x-coordinate
    int   valueToX(float v) const;
    float xToValue(int x)   const;
};
