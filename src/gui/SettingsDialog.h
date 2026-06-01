#pragma once
// SettingsDialog.h — Preferences (default brush, colormap, PET isotope).

#include <QDialog>

class QSpinBox;
class QComboBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    int    brushRadius() const;
    int    colormap()    const;
    double halfLifeS()   const;   // selected isotope half-life (seconds)

private:
    QSpinBox*  m_brush{nullptr};
    QComboBox* m_cmap{nullptr};
    QComboBox* m_iso{nullptr};

    void persist();
};
