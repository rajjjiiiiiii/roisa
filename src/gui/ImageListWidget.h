#pragma once
// ImageListWidget.h — Compact panel showing all loaded images with
// visibility toggles, REF/IN badges, click-to-activate, and remove buttons.
//
// Ownership: MainWindow creates this widget and owns the actual ROIVolume
// objects.  This widget only holds display state (names, eye flags, active
// index) and emits signals when the user interacts.

#include <QWidget>
#include <QVector>
#include <QString>

class QVBoxLayout;
class QPushButton;
class QLabel;

class ImageListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImageListWidget(QWidget* parent = nullptr);

    // ── Population ────────────────────────────────────────────────────────────
    void clear();
    /// Append one row.  isRef=true → blue "REF" badge; false → orange "IN n" badge.
    void addImage(const QString& displayName, bool isRef);

    // ── State setters (call after addImage or after data changes) ─────────────
    void setActive (int idx);               // highlight row; -1 = none
    void setEnabled(int idx, bool on);      // update eye glyph + dim row
    void setRemoveEnabled(int idx, bool on);// disable ✕ on reference row

signals:
    void activateRequested (int idx);
    void visibilityToggled (int idx, bool on);
    void removeRequested   (int idx);
    void addRequested      ();

private:
    struct Row {
        QWidget*     widget   {nullptr};
        QPushButton* eyeBtn   {nullptr};
        QLabel*      badge    {nullptr};
        QPushButton* nameBtn  {nullptr};  // flat, styled like label, click=activate
        QPushButton* removeBtn{nullptr};
        bool         enabled  {true};
    };

    QVBoxLayout*   m_listLayout{nullptr};
    QVector<Row>   m_rows;
    int            m_activeIdx{-1};

    void applyRowStyle(int idx);
};
