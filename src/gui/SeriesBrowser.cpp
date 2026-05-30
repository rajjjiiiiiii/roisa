// SeriesBrowser.cpp — DICOM study/series browser and file picker

#include "SeriesBrowser.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkMetaDataObject.h>

// ── Constructor ───────────────────────────────────────────────────────────────

SeriesBrowser::SeriesBrowser(QWidget* parent) : QWidget(parent)
{
    setMinimumWidth(220);
    setMaximumWidth(320);

    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);
    vl->setSpacing(4);

    auto* topRow = new QHBoxLayout;
    auto* openBtn = new QPushButton("Open Folder…", this);
    openBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    topRow->addWidget(openBtn);
    vl->addLayout(topRow);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Filter series…");
    vl->addWidget(m_filterEdit);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabel("Studies / Series");
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setStyleSheet(
        "QTreeWidget{background:#1c1c1c;color:#ddd;border:none;}"
        "QTreeWidget::item:selected{background:#2a5080;}"
        "QTreeWidget::item:hover{background:#2a2a2a;}");
    vl->addWidget(m_tree, 1);

    vl->addWidget(new QLabel("Double-click a series to load", this));

    connect(openBtn,    &QPushButton::clicked,       this, &SeriesBrowser::onOpenFolder);
    connect(m_tree,     &QTreeWidget::itemDoubleClicked,
            this, &SeriesBrowser::onItemDoubleClicked);
    connect(m_filterEdit, &QLineEdit::textChanged,   this, &SeriesBrowser::onFilterChanged);
}

// ── Open folder ───────────────────────────────────────────────────────────────

void SeriesBrowser::onOpenFolder()
{
    QString dir = QFileDialog::getExistingDirectory(
                      this, "Select image directory", m_lastDir);
    if (!dir.isEmpty()) scanDirectory(dir);
}

// ── scanDirectory ─────────────────────────────────────────────────────────────

void SeriesBrowser::scanDirectory(const QString& dir)
{
    m_tree->clear();
    m_lastDir = dir;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    scanDicom(dir);
    scanOtherFiles(dir);
    QApplication::restoreOverrideCursor();
    m_tree->expandAll();
    applyFilter(m_filterEdit->text());
    emit directoryScanned(dir);
}

// ── DICOM scan ────────────────────────────────────────────────────────────────

namespace {
// Read one string tag from a GDCMImageIO MetaDataDictionary; returns "" on miss.
static QString getTag(const itk::MetaDataDictionary& dict, const std::string& tag)
{
    std::string val;
    if (!itk::ExposeMetaData<std::string>(dict, tag, val)) return {};
    // Trim trailing spaces and null bytes (DICOM even-padding)
    while (!val.empty() && (val.back() == ' ' || val.back() == '\0'))
        val.pop_back();
    return QString::fromStdString(val).trimmed();
}
} // namespace

void SeriesBrowser::scanDicom(const QString& dir)
{
    auto nameGen = itk::GDCMSeriesFileNames::New();
    nameGen->SetUseSeriesDetails(true);
    nameGen->SetDirectory(dir.toStdString());
    const auto& uids = nameGen->GetSeriesUIDs();
    if (uids.empty()) return;

    // key → QTreeWidgetItem* for patients and studies
    QMap<QString, QTreeWidgetItem*> patItems, studyItems;

    for (const auto& uid : uids) {
        auto files = nameGen->GetFileNames(uid);
        if (files.empty()) continue;

        // Read header of first file only (no pixel data)
        QString patName, patID, studyDate, studyDesc, serDesc, modality, serNum;
        try {
            auto io = itk::GDCMImageIO::New();
            io->SetFileName(files.front());
            io->ReadImageInformation();
            const auto& dict = io->GetMetaDataDictionary();
            patName   = getTag(dict, "0010|0010");
            patID     = getTag(dict, "0010|0020");
            studyDate = getTag(dict, "0008|0020");
            studyDesc = getTag(dict, "0008|1030");
            serDesc   = getTag(dict, "0008|103e");
            modality  = getTag(dict, "0008|0060");
            serNum    = getTag(dict, "0020|0011");
        } catch (...) {}

        // ── Patient item ──────────────────────────────────────────────────
        QString patKey = patID.isEmpty() ? patName : patID;
        if (patKey.isEmpty()) patKey = "unknown";
        if (!patItems.contains(patKey)) {
            QString label = patName.isEmpty() ? "Unknown Patient" : patName;
            if (!patID.isEmpty()) label += "  [" + patID + "]";
            auto* pi = new QTreeWidgetItem(m_tree, {label});
            pi->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogDetailedView));
            patItems[patKey] = pi;
        }

        // ── Study item ────────────────────────────────────────────────────
        QString studyKey = patKey + "|" + studyDate + "|" + studyDesc;
        if (!studyItems.contains(studyKey)) {
            QString label = studyDate;
            if (!studyDesc.isEmpty()) label += "  " + studyDesc;
            if (label.trimmed().isEmpty()) label = "Unknown Study";
            auto* si = new QTreeWidgetItem(patItems[patKey], {label});
            si->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            studyItems[studyKey] = si;
        }

        // ── Series item ───────────────────────────────────────────────────
        QString serLabel = "[" + (modality.isEmpty() ? "??" : modality) + "]  ";
        if (!serNum.isEmpty())  serLabel += serNum + ": ";
        serLabel += serDesc.isEmpty() ? "Series" : serDesc;
        serLabel += QString("  (%1 slices)").arg((int)files.size());

        auto* ri = new QTreeWidgetItem(studyItems[studyKey], {serLabel});
        ri->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        ri->setData(0, Qt::UserRole,   dir);                              // directory
        ri->setData(0, Qt::UserRole+1, QString::fromStdString(uid));      // series UID
        ri->setToolTip(0, QString("UID: %1").arg(QString::fromStdString(uid)));
    }
}

// ── Non-DICOM files ───────────────────────────────────────────────────────────

void SeriesBrowser::scanOtherFiles(const QString& dir)
{
    static const QStringList EXTS = {
        "*.nii", "*.nii.gz", "*.mhd", "*.mha", "*.nrrd",
        "*.png",  "*.tiff",   "*.tif", "*.hdr"
    };

    QDir d(dir);
    QStringList files = d.entryList(EXTS, QDir::Files, QDir::Name);
    // Also check one level deep (common scanner export layout)
    QDirIterator it(dir, EXTS, QDir::Files,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    QSet<QString> seen(files.begin(), files.end());
    while (it.hasNext()) {
        QString rel = QDir(dir).relativeFilePath(it.next());
        if (!seen.contains(rel)) { files << rel; seen.insert(rel); }
    }
    if (files.isEmpty()) return;

    auto* otherItem = new QTreeWidgetItem(m_tree, {"Other Images"});
    otherItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    files.sort();
    for (const auto& f : files) {
        auto* fi = new QTreeWidgetItem(otherItem, {f});
        fi->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        fi->setData(0, Qt::UserRole+2, dir + "/" + f);   // full file path
    }
}

// ── Signals ───────────────────────────────────────────────────────────────────

void SeriesBrowser::onItemDoubleClicked(QTreeWidgetItem* item, int)
{
    if (!item) return;
    QString dicomDir  = item->data(0, Qt::UserRole).toString();
    QString uid       = item->data(0, Qt::UserRole+1).toString();
    QString filePath  = item->data(0, Qt::UserRole+2).toString();

    if (!filePath.isEmpty())
        emit fileRequested(filePath);
    else if (!dicomDir.isEmpty() && !uid.isEmpty())
        emit dicomSeriesRequested(dicomDir, uid);
}

// ── Filter ────────────────────────────────────────────────────────────────────

void SeriesBrowser::onFilterChanged(const QString& text)
{
    applyFilter(text);
}

void SeriesBrowser::applyFilter(const QString& text)
{
    const bool showAll = text.trimmed().isEmpty();
    // Walk every leaf item; hide if text not found in label
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* top = m_tree->topLevelItem(i);
        bool anyTop = false;
        for (int j = 0; j < top->childCount(); ++j) {
            auto* study = top->child(j);
            bool anyStudy = false;
            for (int k = 0; k < study->childCount(); ++k) {
                auto* ser = study->child(k);
                bool vis = showAll || ser->text(0).contains(text, Qt::CaseInsensitive);
                ser->setHidden(!vis);
                if (vis) anyStudy = true;
            }
            // Also match non-DICOM leaf under "Other Images"
            if (study->childCount() == 0) {
                bool vis = showAll || study->text(0).contains(text, Qt::CaseInsensitive);
                study->setHidden(!vis);
                if (vis) anyTop = true;
                continue;
            }
            study->setHidden(!anyStudy);
            if (anyStudy) anyTop = true;
        }
        top->setHidden(!anyTop && !showAll);
    }
}
