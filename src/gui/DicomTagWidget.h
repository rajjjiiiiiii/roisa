#pragma once
// DicomTagWidget.h — Filterable DICOM / image-header tag inspector.

#include <QVector>
#include <QWidget>

class QLineEdit;
class QTableWidget;
class ROIVolume;

class DicomTagWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DicomTagWidget(QWidget* parent = nullptr);

    void setVolume(ROIVolume* vol);

    /// (Re)read tags from the loaded volume. No-op when no image is loaded.
    void refresh();

    void clear();

private slots:
    void onFilterChanged(const QString& text);

private:
    QTableWidget* m_table      {nullptr};
    QLineEdit*    m_filterEdit {nullptr};
    ROIVolume*    m_vol        {nullptr};

    struct TagEntry { QString tag, name, value; };
    QVector<TagEntry> m_allTags;

    void loadDicomTags (const std::string& firstFile);
    void loadVolumeInfo();
    void rebuildTable  ();
    void applyFilter   (const QString& text);

    static QString humanName(const QString& tag);
};
