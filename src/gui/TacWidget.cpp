// TacWidget.cpp — painter-based time-activity-curve plot.

#include "TacWidget.h"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

TacWidget::TacWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(140);
    setStyleSheet("background:#141414;");
}

void TacWidget::setValues(const std::vector<double>& values, const QString& ylabel)
{
    m_values.clear();
    for (double v : values)
        if (!std::isnan(v)) m_values.push_back(v);
    m_ylabel = ylabel;
    update();
}

void TacWidget::clearValues()
{
    m_values.clear();
    update();
}

void TacWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 20));
    QFont f = font(); f.setPointSize(8); p.setFont(f);

    const int w = width(), h = height();
    const int ml = 40, mr = 10, mt = 18, mb = 22;
    const int x0 = ml, y0 = mt;
    const int pw = std::max(1, w - ml - mr), ph = std::max(1, h - mt - mb);

    p.setPen(QPen(QColor(90, 90, 90), 1));
    p.drawLine(x0, y0, x0, y0 + ph);
    p.drawLine(x0, y0 + ph, x0 + pw, y0 + ph);

    p.setPen(QColor(150, 150, 150));
    p.drawText(x0, 12, QString("Time-Activity Curve   (%1)").arg(m_ylabel));

    const int n = static_cast<int>(m_values.size());
    if (n == 0) {
        p.setPen(QColor(110, 110, 110));
        p.drawText(rect(), Qt::AlignCenter,
                   "No curve — draw an ROI, load frames, Compute");
        return;
    }

    double vmax = *std::max_element(m_values.begin(), m_values.end());
    double vmin = *std::min_element(m_values.begin(), m_values.end());
    if (vmax - vmin < 1e-9) vmax = vmin + 1.0;
    const double rng = vmax - vmin;

    p.setPen(QColor(120, 120, 120));
    p.drawText(2, y0 + 6,  QString::number(vmax, 'f', 2));
    p.drawText(2, y0 + ph, QString::number(vmin, 'f', 2));

    auto pt = [&](int i) -> QPointF {
        double x = (n > 1) ? x0 + (double)pw * i / (n - 1) : x0 + pw / 2.0;
        double y = y0 + ph - ph * ((m_values[i] - vmin) / rng);
        return QPointF(x, y);
    };

    QPolygonF poly;
    for (int i = 0; i < n; ++i) poly << pt(i);
    p.setPen(QPen(QColor(120, 200, 255), 2));
    p.drawPolyline(poly);

    for (int i = 0; i < n; ++i) {
        QPointF q = pt(i);
        p.setBrush(QColor(255, 210, 90));
        p.setPen(QPen(QColor(255, 210, 90), 1));
        p.drawEllipse(q, 3, 3);
        p.setPen(QColor(170, 170, 170));
        p.drawText(int(q.x()) - 8, y0 + ph + 14, QString::number(i + 1));
    }
}
