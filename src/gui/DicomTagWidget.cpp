// DicomTagWidget.cpp — DICOM / image-header metadata inspector

#include "DicomTagWidget.h"
#include "../core/ROIVolume.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <itkGDCMImageIO.h>
#include <itkMetaDataObject.h>

// ── Human-readable DICOM tag names (most common) ──────────────────────────────
/*static*/ QString DicomTagWidget::humanName(const QString& tag)
{
    static const QHash<QString,QString> MAP = {
        {"0008|0016","SOP Class UID"},       {"0008|0018","SOP Instance UID"},
        {"0008|0020","Study Date"},           {"0008|0021","Series Date"},
        {"0008|0030","Study Time"},           {"0008|0031","Series Time"},
        {"0008|0060","Modality"},             {"0008|0070","Manufacturer"},
        {"0008|0080","Institution Name"},     {"0008|0090","Referring Physician"},
        {"0008|103e","Series Description"},   {"0008|1030","Study Description"},
        {"0008|1090","Manufacturer Model"},   {"0010|0010","Patient Name"},
        {"0010|0020","Patient ID"},           {"0010|0030","Patient Birth Date"},
        {"0010|0040","Patient Sex"},          {"0010|1010","Patient Age"},
        {"0010|1030","Patient Weight"},       {"0018|0050","Slice Thickness"},
        {"0018|0080","Repetition Time"},      {"0018|0081","Echo Time"},
        {"0018|0087","Magnetic Field Strength"},
        {"0018|0088","Spacing Between Slices"},
        {"0018|1030","Protocol Name"},        {"0018|5100","Patient Position"},
        {"0020|000d","Study Instance UID"},   {"0020|000e","Series Instance UID"},
        {"0020|0010","Study ID"},             {"0020|0011","Series Number"},
        {"0020|0013","Instance Number"},      {"0020|0032","Image Position"},
        {"0020|0037","Image Orientation"},    {"0020|0052","Frame of Reference UID"},
        {"0028|0010","Rows"},                 {"0028|0011","Columns"},
        {"0028|0030","Pixel Spacing"},        {"0028|0100","Bits Allocated"},
        {"0028|0101","Bits Stored"},          {"0028|0103","Pixel Representation"},
        {"0028|1050","Window Center"},        {"0028|1051","Window Width"},
        {"0028|1052","Rescale Intercept"},    {"0028|1053","Rescale Slope"},
    };
    return MAP.value(tag, "");
}

// ── Constructor ───────────────────────────────────────────────────────────────

DicomTagWidget::DicomTagWidget(QWidget* parent) : QWidget(parent)
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(4);

    auto* row = new QHBoxLayout;
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Filter tags…");
    row->addWidget(m_filterEdit);
    auto* refreshBtn = new QPushButton("Refresh", this);
    refreshBtn->setMaximumWidth(65);
    row->addWidget(refreshBtn);
    vl->addLayout(row);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({"Tag", "Name", "Value"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->setStyleSheet(
        "QTableWidget{background:#1c1c1c;color:#ddd;border:none;}"
        "QTableWidget::item:selected{background:#2a5080;}");
    vl->addWidget(m_table, 1);

    connect(m_filterEdit, &QLineEdit::textChanged, this, &DicomTagWidget::onFilterChanged);
    connect(refreshBtn,   &QPushButton::clicked,   this, &DicomTagWidget::refresh);
}

// ── Public API ────────────────────────────────────────────────────────────────

void DicomTagWidget::setVolume(ROIVolume* vol)
{
    m_vol = vol;
    refresh();
}

void DicomTagWidget::refresh()
{
    m_allTags.clear();
    if (!m_vol || !m_vol->isLoaded()) { rebuildTable(); return; }

    if (!m_vol->firstDicomFile().empty())
        loadDicomTags(m_vol->firstDicomFile());
    else
        loadVolumeInfo();

    rebuildTable();
    applyFilter(m_filterEdit->text());
}

void DicomTagWidget::clear()
{
    m_allTags.clear();
    m_table->setRowCount(0);
}

// ── Internal loaders ─────────────────────────────────────────────────────────

void DicomTagWidget::loadDicomTags(const std::string& firstFile)
{
    try {
        auto io = itk::GDCMImageIO::New();
        io->SetFileName(firstFile);
        io->ReadImageInformation();
        const auto& dict = io->GetMetaDataDictionary();

        for (auto it = dict.Begin(); it != dict.End(); ++it) {
            std::string raw;
            if (!itk::ExposeMetaData<std::string>(dict, it->first, raw)) continue;
            // Trim
            while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\0'))
                raw.pop_back();
            QString tag   = QString::fromStdString(it->first).toLower();
            QString name  = humanName(tag);
            QString value = QString::fromStdString(raw);
            m_allTags.append({tag, name, value});
        }
    } catch (const std::exception& e) {
        m_allTags.append({"error", "Load error", QString::fromLocal8Bit(e.what())});
    }
}

void DicomTagWidget::loadVolumeInfo()
{
    // NIfTI / MetaImage: show basic volume info from ROIVolume
    auto add = [&](const QString& tag, const QString& name, const QString& val) {
        m_allTags.append({tag, name, val});
    };
    add("roisa|nx",   "Dimension X",    QString::number(m_vol->nx()));
    add("roisa|ny",   "Dimension Y",    QString::number(m_vol->ny()));
    add("roisa|nz",   "Dimension Z",    QString::number(m_vol->nz()));
    add("roisa|sp",   "Voxel spacing",  QString("%1 mm (isotropic)")
                                            .arg(m_vol->voxelSpacingMm(), 0, 'f', 4));
    add("roisa|vmin", "Intensity min",  QString::number(m_vol->vmin(), 'f', 1));
    add("roisa|vmax", "Intensity max",  QString::number(m_vol->vmax(), 'f', 1));
}

// ── Table management ──────────────────────────────────────────────────────────

void DicomTagWidget::rebuildTable()
{
    m_table->setRowCount(m_allTags.size());
    for (int i = 0; i < m_allTags.size(); ++i) {
        m_table->setItem(i, 0, new QTableWidgetItem(m_allTags[i].tag));
        m_table->setItem(i, 1, new QTableWidgetItem(m_allTags[i].name));
        m_table->setItem(i, 2, new QTableWidgetItem(m_allTags[i].value));
        m_table->setRowHeight(i, 18);
    }
}

void DicomTagWidget::applyFilter(const QString& text)
{
    bool showAll = text.trimmed().isEmpty();
    for (int i = 0; i < m_table->rowCount(); ++i) {
        bool vis = showAll
            || m_table->item(i,0)->text().contains(text, Qt::CaseInsensitive)
            || m_table->item(i,1)->text().contains(text, Qt::CaseInsensitive)
            || m_table->item(i,2)->text().contains(text, Qt::CaseInsensitive);
        m_table->setRowHidden(i, !vis);
    }
}

void DicomTagWidget::onFilterChanged(const QString& text)
{
    applyFilter(text);
}
