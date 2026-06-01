// SettingsDialog.cpp — Preferences dialog.

#include "SettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSettings>
#include <QSpinBox>
#include <array>

namespace {
// name, half-life (seconds)
const std::array<std::pair<const char*, double>, 7> ISOTOPES = {{
    {"F-18  (6586 s)",   6586.2},
    {"C-11  (1223 s)",   1223.4},
    {"N-13  (598 s)",     597.9},
    {"O-15  (122 s)",     122.24},
    {"Ga-68 (4062 s)",   4062.0},
    {"Cu-64 (45720 s)", 45720.0},
    {"Zr-89 (282240 s)",282240.0},
}};
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Preferences");
    QSettings s("ROISA", "ROISA");
    auto* form = new QFormLayout(this);

    m_brush = new QSpinBox; m_brush->setRange(1, 50);
    m_brush->setValue(s.value("brushRadius", 3).toInt());
    form->addRow("Default brush radius", m_brush);

    m_cmap = new QComboBox; m_cmap->addItems({"Gray", "Hot", "Cool", "Viridis"});
    m_cmap->setCurrentIndex(s.value("colormap", 0).toInt());
    form->addRow("Default colormap", m_cmap);

    m_iso = new QComboBox;
    for (const auto& it : ISOTOPES) m_iso->addItem(it.first);
    m_iso->setCurrentIndex(s.value("isotope", 0).toInt());
    form->addRow("PET isotope", m_iso);

    auto* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, [this]{ persist(); accept(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(bb);
}

void SettingsDialog::persist()
{
    QSettings s("ROISA", "ROISA");
    s.setValue("brushRadius", m_brush->value());
    s.setValue("colormap",    m_cmap->currentIndex());
    s.setValue("isotope",     m_iso->currentIndex());
}

int    SettingsDialog::brushRadius() const { return m_brush->value(); }
int    SettingsDialog::colormap()    const { return m_cmap->currentIndex(); }
double SettingsDialog::halfLifeS()   const { return ISOTOPES[m_iso->currentIndex()].second; }
