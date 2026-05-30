// SliceView.cpp — Rendering and mouse handling for one orthogonal slice panel

#include "SliceView.h"

#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

// ── Label colour table ────────────────────────────────────────────────────────

namespace {

static const uint8_t TAB20[20][3] = {
    {31,119,180},{174,199,232},{255,127,14},{255,187,120},
    {44,160,44},{152,223,138},{214,39,40},{255,152,150},
    {148,103,189},{197,176,213},{140,86,75},{196,156,148},
    {227,119,194},{247,182,210},{127,127,127},{199,199,199},
    {188,189,34},{219,219,141},{23,190,207},{158,218,229},
};

static void hsvToRgb(float h,float s,float v,uint8_t& r,uint8_t& g,uint8_t& b)
{
    int   hi = static_cast<int>(h*6.f)%6;
    float f  = h*6.f - std::floor(h*6.f);
    float p=v*(1-s), q=v*(1-f*s), t=v*(1-(1-f)*s);
    float rv,gv,bv;
    switch(hi){
        case 0:rv=v;gv=t;bv=p;break; case 1:rv=q;gv=v;bv=p;break;
        case 2:rv=p;gv=v;bv=t;break; case 3:rv=p;gv=q;bv=v;break;
        case 4:rv=t;gv=p;bv=v;break; default:rv=v;gv=p;bv=q;
    }
    r=(uint8_t)(rv*255); g=(uint8_t)(gv*255); b=(uint8_t)(bv*255);
}

} // namespace

/*static*/ std::array<uint8_t,4> SliceView::labelColor(int label)
{
    constexpr uint8_t ALPHA = 115; // ~0.45
    int idx = label - 1;
    if (idx < 0)  return {0,0,0,0};
    if (idx < 20) return {TAB20[idx][0],TAB20[idx][1],TAB20[idx][2],ALPHA};
    static constexpr float phi = 1.6180339887f;
    float h = static_cast<float>((idx-20)*phi);
    h -= std::floor(h);
    float s=0.70f, v=((idx-20)/7%2==0)?0.75f:0.95f;
    uint8_t r,g,b; hsvToRgb(h,s,v,r,g,b);
    return {r,g,b,ALPHA};
}

// ── Constructor ───────────────────────────────────────────────────────────────

SliceView::SliceView(int axis, QWidget* parent)
    : QWidget(parent), m_axis(axis)
{
    setMinimumSize(200, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void SliceView::setVolume(ROIVolume* vol)
{
    m_vol = vol;
    if (vol && vol->isLoaded()) {
        int maxIdx = (m_axis==0 ? vol->nx() : m_axis==1 ? vol->ny() : vol->nz()) - 1;
        m_sliceIdx = maxIdx / 2;
        m_crossA = (m_axis==0 ? vol->ny() : vol->nx()) / 2;
        m_crossB = (m_axis==2 ? vol->ny() : vol->nz()) / 2;
    }
    update();
}

void SliceView::setSliceIndex(int idx)
{
    if (!m_vol) return;
    int maxIdx = (m_axis==0?m_vol->nx():m_axis==1?m_vol->ny():m_vol->nz()) - 1;
    m_sliceIdx = std::max(0, std::min(idx, maxIdx));
    update();
}

void SliceView::setCrosshair(int a, int b)
{
    m_crossA = a; m_crossB = b;
    update();
}

// ── Layout ────────────────────────────────────────────────────────────────────

QRect SliceView::imageRect() const
{
    if (!m_vol || !m_vol->isLoaded()) return rect();
    int iw = m_vol->sliceCols(m_axis);
    int ih = m_vol->sliceRows(m_axis);
    if (iw==0||ih==0) return rect();
    double sx = (double)width()  / iw;
    double sy = (double)height() / ih;
    double sc = std::min(sx, sy);
    int dw = (int)(iw*sc), dh = (int)(ih*sc);
    return {(width()-dw)/2, (height()-dh)/2, dw, dh};
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void SliceView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(20,20,20));

    if (!m_vol || !m_vol->isLoaded()) {
        painter.setPen(Qt::gray);
        static const char* NAMES[] = {"Sagittal","Coronal","Axial"};
        painter.drawText(rect(), Qt::AlignCenter,
                         QString("%1\n(no image)").arg(NAMES[m_axis]));
        return;
    }

    int rows = m_vol->sliceRows(m_axis);
    int cols = m_vol->sliceCols(m_axis);

    m_intensityBuf.resize(rows*cols);
    m_maskBuf.resize(rows*cols);
    m_vol->getIntensitySlice(m_axis, m_sliceIdx, m_intensityBuf);
    m_vol->getMaskSlice     (m_axis, m_sliceIdx, m_maskBuf);

    // Build RGB32 image
    QImage img(cols, rows, QImage::Format_RGB32);
    float vmin  = m_vol->vmin(), vmax = m_vol->vmax();
    float range = vmax - vmin;
    if (range < 1e-6f) range = 1e-6f;

    for (int row = 0; row < rows; ++row) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(row));
        for (int col = 0; col < cols; ++col) {
            int gray = (int)(255.f * (m_intensityBuf[row*cols+col]-vmin) / range);
            gray = std::max(0, std::min(255, gray));
            int16_t lbl = m_maskBuf[row*cols+col];
            if (lbl > 0) {
                auto [lr,lg,lb,la] = labelColor(lbl);
                float a = la/255.f;
                int r = std::min(255,(int)(gray*(1-a)+lr*a));
                int g = std::min(255,(int)(gray*(1-a)+lg*a));
                int b = std::min(255,(int)(gray*(1-a)+lb*a));
                line[col] = qRgb(r,g,b);
            } else {
                line[col] = qRgb(gray,gray,gray);
            }
        }
    }

    QRect ir = imageRect();
    painter.drawImage(ir, img);

    // Crosshair
    float scX = (float)ir.width()  / cols;
    float scY = (float)ir.height() / rows;
    int crossRow = (int)(m_crossA * scY) + ir.top();
    int crossCol = (int)(m_crossB * scX) + ir.left();
    QPen chPen(QColor(255,230,0,180), 1, Qt::DashLine);
    painter.setPen(chPen);
    painter.drawLine(ir.left(),  crossRow, ir.right(),  crossRow);
    painter.drawLine(crossCol,   ir.top(), crossCol,    ir.bottom());

    // Label
    static const char* NAMES[] = {"Sagittal","Coronal","Axial"};
    painter.setPen(QColor(200,200,200));
    QFont f = font(); f.setPointSize(8); painter.setFont(f);
    painter.drawText(ir.adjusted(4,4,0,0), Qt::AlignTop|Qt::AlignLeft,
                     QString("%1  [%2]").arg(NAMES[m_axis]).arg(m_sliceIdx));
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

bool SliceView::widgetToVoxel(int wx, int wy,
                               int& vx, int& vy, int& vz) const
{
    if (!m_vol || !m_vol->isLoaded()) return false;
    QRect ir = imageRect();
    if (!ir.contains(wx, wy)) return false;
    int cols = m_vol->sliceCols(m_axis);
    int rows = m_vol->sliceRows(m_axis);
    int col = (int)((wx-ir.left()) * cols / (double)ir.width());
    int row = (int)((wy-ir.top())  * rows / (double)ir.height());
    col = std::max(0, std::min(cols-1, col));
    row = std::max(0, std::min(rows-1, row));
    // axis 0 (sag x=idx): row=y, col=z
    // axis 1 (cor y=idx): row=x, col=z
    // axis 2 (axi z=idx): row=x, col=y
    if      (m_axis==0) { vx=m_sliceIdx; vy=row; vz=col; }
    else if (m_axis==1) { vx=row; vy=m_sliceIdx; vz=col; }
    else                { vx=row; vy=col;  vz=m_sliceIdx; }
    return true;
}

void SliceView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }
    m_dragging = true;
    int vx,vy,vz;
    if (widgetToVoxel(e->pos().x(), e->pos().y(), vx,vy,vz))
        emit sliceClicked(vx,vy,vz);
}

void SliceView::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) { QWidget::mouseMoveEvent(e); return; }
    int vx,vy,vz;
    if (widgetToVoxel(e->pos().x(), e->pos().y(), vx,vy,vz))
        emit sliceDragged(vx,vy,vz);
}

void SliceView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button()==Qt::LeftButton) { m_dragging=false; emit sliceReleased(); }
    QWidget::mouseReleaseEvent(e);
}

void SliceView::wheelEvent(QWheelEvent* e)
{
    emit scrolled(e->angleDelta().y() > 0 ? 1 : -1);
    e->accept();
}
