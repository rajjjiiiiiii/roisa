// ImageListWidget.cpp

#include "ImageListWidget.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// ── Style constants ───────────────────────────────────────────────────────────

static const char* kEyeOn     = "●";
static const char* kEyeOff    = "○";
static const char* kRemove    = "✕";

static QString eyeBtnStyle(bool on) {
    return on
        ? "QPushButton{color:#6af;background:transparent;border:none;font-size:11px;}"
        : "QPushButton{color:#445;background:transparent;border:none;font-size:11px;}";
}
static QString refBadgeStyle() {
    return "QLabel{background:#1a5fb4;color:#fff;border-radius:3px;"
           "padding:1px 3px;font-size:8px;font-weight:bold;}";
}
static QString inBadgeStyle() {
    return "QLabel{background:#c06000;color:#fff;border-radius:3px;"
           "padding:1px 3px;font-size:8px;font-weight:bold;}";
}
static QString nameBtnStyle(bool active) {
    return active
        ? "QPushButton{color:#eee;background:transparent;border:none;"
          "font-size:10px;text-align:left;padding:0 2px;}"
          "QPushButton:hover{color:#fff;}"
        : "QPushButton{color:#aaa;background:transparent;border:none;"
          "font-size:10px;text-align:left;padding:0 2px;}"
          "QPushButton:hover{color:#ddd;}";
}
static QString removeBtnStyle() {
    return "QPushButton{color:#844;background:transparent;border:none;font-size:9px;}"
           "QPushButton:hover{color:#f66;}"
           "QPushButton:disabled{color:#2a2a2a;}";
}
static QString rowStyle(bool active) {
    return active
        ? "QWidget{background:#1a3d5c;border-radius:3px;}"
        : "QWidget{background:transparent;}";
}

// ═════════════════════════════════════════════════════════════════════════════

ImageListWidget::ImageListWidget(QWidget* parent) : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 2);
    outer->setSpacing(3);

    // ── Header: "Images" label + "+ Add" button ───────────────────────────────
    auto* hdr  = new QWidget(this);
    auto* hdrL = new QHBoxLayout(hdr);
    hdrL->setContentsMargins(0, 0, 0, 0);
    hdrL->setSpacing(4);

    auto* titleLbl = new QLabel("Images", hdr);
    titleLbl->setStyleSheet("color:#9a9; font-size:10px; font-weight:bold;");
    hdrL->addWidget(titleLbl);
    hdrL->addStretch();

    auto* addBtn = new QPushButton("＋ Add", hdr);
    addBtn->setFixedHeight(18);
    addBtn->setStyleSheet(
        "QPushButton{background:#1a3d1a;color:#8c8;border:1px solid #3a6a3a;"
        "border-radius:2px;padding:1px 8px;font-size:10px;}"
        "QPushButton:hover{background:#246024;color:#afa;}");
    connect(addBtn, &QPushButton::clicked, this, &ImageListWidget::addRequested);
    hdrL->addWidget(addBtn);
    outer->addWidget(hdr);

    // ── Thin separator ────────────────────────────────────────────────────────
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("border:none;background:#2a2a2a;max-height:1px;");
    outer->addWidget(sep);

    // ── Scrollable row area ────────────────────────────────────────────────────
    auto* listContainer = new QWidget(this);
    listContainer->setStyleSheet(
        "QWidget{background:#181818;border:1px solid #2a2a2a;border-radius:3px;}");
    m_listLayout = new QVBoxLayout(listContainer);
    m_listLayout->setContentsMargins(2, 2, 2, 2);
    m_listLayout->setSpacing(1);
    outer->addWidget(listContainer);

    // Start with placeholder
    auto* empty = new QLabel("No images loaded — use File → Open", listContainer);
    empty->setStyleSheet("color:#444; font-size:9px; padding:6px 4px;");
    empty->setAlignment(Qt::AlignCenter);
    m_listLayout->addWidget(empty);
}

// ── Population ────────────────────────────────────────────────────────────────

void ImageListWidget::clear()
{
    m_rows.clear();
    m_activeIdx = -1;

    while (QLayoutItem* item = m_listLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
}

void ImageListWidget::addImage(const QString& displayName, bool isRef)
{
    const int idx = m_rows.size();

    auto* row  = new QWidget(this);
    auto* rowL = new QHBoxLayout(row);
    rowL->setContentsMargins(3, 1, 3, 1);
    rowL->setSpacing(3);

    // Eye toggle
    auto* eyeBtn = new QPushButton(kEyeOn, row);
    eyeBtn->setFixedSize(16, 16);
    eyeBtn->setFlat(true);
    eyeBtn->setStyleSheet(eyeBtnStyle(true));
    eyeBtn->setToolTip("Toggle visibility");
    rowL->addWidget(eyeBtn);

    // Badge
    QString badgeText = isRef ? "REF" : QString("IN%1").arg(idx);
    auto* badge = new QLabel(badgeText, row);
    badge->setFixedWidth(28);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(isRef ? refBadgeStyle() : inBadgeStyle());
    rowL->addWidget(badge);

    // Name (flat clickable button)
    QString shortName = QFileInfo(displayName).fileName();
    if (shortName.isEmpty()) shortName = displayName;
    if (shortName.length() > 22) shortName = shortName.left(10) + "…" + shortName.right(9);

    auto* nameBtn = new QPushButton(shortName, row);
    nameBtn->setFlat(true);
    nameBtn->setToolTip(displayName);
    nameBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    nameBtn->setStyleSheet(nameBtnStyle(false));
    rowL->addWidget(nameBtn, 1);

    // Remove button
    auto* rmBtn = new QPushButton(kRemove, row);
    rmBtn->setFixedSize(16, 16);
    rmBtn->setFlat(true);
    rmBtn->setStyleSheet(removeBtnStyle());
    rmBtn->setToolTip(isRef ? "Cannot remove reference while inputs exist"
                             : "Remove this input image");
    rowL->addWidget(rmBtn);

    // Connections (capture idx by value)
    connect(eyeBtn, &QPushButton::clicked, this, [this, idx]{
        const bool cur = m_rows[idx].enabled;
        emit visibilityToggled(idx, !cur);
    });
    connect(nameBtn, &QPushButton::clicked, this, [this, idx]{
        emit activateRequested(idx);
    });
    connect(rmBtn, &QPushButton::clicked, this, [this, idx]{
        emit removeRequested(idx);
    });

    Row r; r.widget=row; r.eyeBtn=eyeBtn; r.badge=badge;
           r.nameBtn=nameBtn; r.removeBtn=rmBtn; r.enabled=true;
    m_rows.append(r);
    m_listLayout->addWidget(row);
}

// ── State setters ─────────────────────────────────────────────────────────────

void ImageListWidget::setActive(int idx)
{
    const int prev = m_activeIdx;
    m_activeIdx    = idx;
    if (prev >= 0 && prev < m_rows.size()) applyRowStyle(prev);
    if (idx  >= 0 && idx  < m_rows.size()) applyRowStyle(idx);
}

void ImageListWidget::setEnabled(int idx, bool on)
{
    if (idx < 0 || idx >= m_rows.size()) return;
    m_rows[idx].enabled = on;
    m_rows[idx].eyeBtn->setText(on ? kEyeOn : kEyeOff);
    m_rows[idx].eyeBtn->setStyleSheet(eyeBtnStyle(on));
    // Dim the whole row slightly when hidden
    m_rows[idx].nameBtn->setStyleSheet(
        nameBtnStyle(idx == m_activeIdx));
    m_rows[idx].widget->setStyleSheet(
        on ? rowStyle(idx == m_activeIdx)
           : "QWidget{background:#111;border-radius:3px;}");
}

void ImageListWidget::setRemoveEnabled(int idx, bool on)
{
    if (idx >= 0 && idx < m_rows.size())
        m_rows[idx].removeBtn->setEnabled(on);
}

// ── Private ───────────────────────────────────────────────────────────────────

void ImageListWidget::applyRowStyle(int idx)
{
    if (idx < 0 || idx >= m_rows.size()) return;
    const bool active  = (idx == m_activeIdx);
    const bool enabled = m_rows[idx].enabled;
    m_rows[idx].widget->setStyleSheet(
        !enabled      ? "QWidget{background:#111;border-radius:3px;}"
        : active      ? rowStyle(true)
                      : rowStyle(false));
    m_rows[idx].nameBtn->setStyleSheet(nameBtnStyle(active));
}
