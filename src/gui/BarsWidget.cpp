// BarsWidget.cpp — painter-based bar/histogram plot.

#include "BarsWidget.h"

#include <QPainter>
#include <algorithm>

BarsWidget::BarsWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(120);
    setStyleSheet("background:#141414;");
}

void BarsWidget::setData(const std::vector<double>& counts, double vmin, double vmax,
                         const QString& title)
{
    m_counts = counts;
    m_vmin = vmin; m_vmax = vmax; m_title = title;
    update();
}

void BarsWidget::clearBars()
{
    m_counts.clear();
    update();
}

void BarsWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 20));
    QFont f = font(); f.setPointSize(8); p.setFont(f);

    const int w = width(), h = height();
    const int ml = 8, mr = 8, mt = 16, mb = 16;
    const int pw = std::max(1, w - ml - mr), ph = std::max(1, h - mt - mb);

    p.setPen(QColor(150, 150, 150));
    p.drawText(ml, 12, m_title);

    const int n = static_cast<int>(m_counts.size());
    if (n == 0) {
        p.setPen(QColor(110, 110, 110));
        p.drawText(rect(), Qt::AlignCenter, "Select a label, then Compute");
        return;
    }

    double cmax = *std::max_element(m_counts.begin(), m_counts.end());
    if (cmax <= 0) cmax = 1.0;
    const double bw = (double)pw / n;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(120, 200, 255));
    for (int i = 0; i < n; ++i) {
        double bh = ph * (m_counts[i] / cmax);
        int x = ml + (int)(i * bw);
        p.drawRect(x, (int)(mt + ph - bh), std::max(1, (int)bw - 1), (int)bh);
    }

    p.setPen(QColor(140, 140, 140));
    p.drawText(ml, h - 3, QString::number(m_vmin, 'f', 1));
    p.drawText(w - mr - 40, h - 3, QString::number(m_vmax, 'f', 1));
}
