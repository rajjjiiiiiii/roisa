// HistogramWidget.cpp — Intensity histogram with draggable W/L handles

#include "HistogramWidget.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

HistogramWidget::HistogramWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(70);
    setMaximumHeight(90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::CrossCursor);
}

void HistogramWidget::setVolume(ROIVolume* vol)
{
    m_vol = vol;
    refresh();
}

void HistogramWidget::refresh()
{
    m_counts.assign(BINS, 0);
    if (!m_vol || !m_vol->isLoaded()) { update(); return; }

    const float* buf = m_vol->displayImage()->GetBufferPointer();
    int n = m_vol->nx() * m_vol->ny() * m_vol->nz();

    m_dataMin =  1e30f;
    m_dataMax = -1e30f;
    for (int i=0; i<n; ++i) {
        m_dataMin = std::min(m_dataMin, buf[i]);
        m_dataMax = std::max(m_dataMax, buf[i]);
    }
    float range = m_dataMax - m_dataMin;
    if (range < 1e-6f) range = 1e-6f;

    for (int i=0; i<n; ++i) {
        int bin = (int)((buf[i]-m_dataMin)/range * (BINS-1));
        bin = std::max(0, std::min(BINS-1, bin));
        ++m_counts[bin];
    }
    update();
}

int HistogramWidget::valueToX(float v) const
{
    float range = m_dataMax - m_dataMin;
    if (range < 1e-6f) return 0;
    return (int)((v - m_dataMin) / range * (width()-2)) + 1;
}

float HistogramWidget::xToValue(int x) const
{
    float range = m_dataMax - m_dataMin;
    return m_dataMin + float(x-1) / float(width()-2) * range;
}

void HistogramWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(30,30,30));

    if (m_counts.empty()) {
        p.setPen(Qt::gray); p.drawText(rect(), Qt::AlignCenter, "No image"); return;
    }

    int maxC = *std::max_element(m_counts.begin(), m_counts.end());
    if (maxC == 0) return;

    int bh = height() - 16;

    // Draw bars (log scale for readability)
    p.setPen(Qt::NoPen);
    float binW = float(width()-2) / BINS;
    for (int i=0; i<BINS; ++i) {
        float logH = m_counts[i] > 0
            ? std::log1p(float(m_counts[i])) / std::log1p(float(maxC))
            : 0.f;
        int hh = (int)(logH * bh);
        p.fillRect(QRectF(1+i*binW, bh-hh, binW, hh), QColor(120,150,180));
    }

    // Shaded W/L region
    if (m_vol) {
        int x0 = valueToX(m_vol->vmin());
        int x1 = valueToX(m_vol->vmax());
        x0 = std::max(1, std::min(width()-2, x0));
        x1 = std::max(1, std::min(width()-2, x1));
        p.fillRect(x0, 0, x1-x0, bh, QColor(255,220,80,40));

        // W/L handle lines
        p.setPen(QPen(QColor(255,200,0), 1));
        p.drawLine(x0, 0, x0, bh);
        p.drawLine(x1, 0, x1, bh);
    }

    // Axis labels
    p.setPen(QColor(160,160,160));
    QFont f = font(); f.setPointSize(7); p.setFont(f);
    p.drawText(1, bh+2, width()/2-2, 12, Qt::AlignLeft,
               QString::number(m_dataMin,'g',4));
    p.drawText(width()/2, bh+2, width()/2-2, 12, Qt::AlignRight,
               QString::number(m_dataMax,'g',4));
}

void HistogramWidget::mousePressEvent(QMouseEvent* e)
{
    if (!m_vol) return;
    int x = e->pos().x();
    int x0 = valueToX(m_vol->vmin());
    int x1 = valueToX(m_vol->vmax());
    // Snap to nearest handle
    if (std::abs(x-x0) < std::abs(x-x1))
        m_drag = VMIN_HANDLE;
    else
        m_drag = VMAX_HANDLE;
}

void HistogramWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag == NONE || !m_vol) return;
    float v = xToValue(e->pos().x());
    float lo = m_vol->vmin(), hi = m_vol->vmax();
    if (m_drag == VMIN_HANDLE) lo = std::min(v, hi - 1.f);
    else                        hi = std::max(v, lo + 1.f);
    m_vol->setWindow(lo, hi);
    emit windowChanged(lo, hi);
    update();
}

void HistogramWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = NONE;
}
