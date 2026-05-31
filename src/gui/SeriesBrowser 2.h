#pragma once
// SeriesBrowser.h — Left-dock panel for browsing DICOM studies/series
//                   and non-DICOM image files in a directory.

#include <QWidget>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QPushButton;

class SeriesBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit SeriesBrowser(QWidget* parent = nullptr);

    /// Scan directory for DICOM series and image files, populate tree.
    void scanDirectory(const QString& dir);

signals:
    /// User double-clicked a DICOM series.
    void dicomSeriesRequested(QString directory, QString seriesUID);
    /// User double-clicked a non-DICOM image file.
    void fileRequested(QString filePath);
    /// New directory scanned — caller may want to record it.
    void directoryScanned(QString directory);

private slots:
    void onOpenFolder();
    void onItemDoubleClicked(QTreeWidgetItem* item, int col);
    void onFilterChanged(const QString& text);

private:
    QTreeWidget* m_tree      {nullptr};
    QLineEdit*   m_filterEdit{nullptr};
    QString      m_lastDir;

    void scanDicom     (const QString& dir);
    void scanOtherFiles(const QString& dir);
    void applyFilter   (const QString& text);
};
