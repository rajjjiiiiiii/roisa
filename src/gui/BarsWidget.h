#pragma once
// BarsWidget.h — Minimal painter-based bar/histogram plot (read-only).

#include <QWidget>
#include <QString>
#include <vector>

class BarsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BarsWidget(QWidget* parent = nullptr);
    void setData(const std::vector<double>& counts, double vmin, double vmax,
                 const QString& title = "ROI histogram");
    void clearBars();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<double> m_counts;
    double  m_vmin{0.0};
    double  m_vmax{1.0};
    QString m_title{"ROI histogram"};
};
