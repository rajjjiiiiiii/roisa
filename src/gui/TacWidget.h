#pragma once
// TacWidget.h — Small painter-based time-activity-curve plot.

#include <QWidget>
#include <QString>
#include <vector>

class TacWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TacWidget(QWidget* parent = nullptr);

    void setValues(const std::vector<double>& values,
                   const QString& ylabel = "SUVmean");
    void clearValues();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<double> m_values;
    QString m_ylabel{"SUVmean"};
};
