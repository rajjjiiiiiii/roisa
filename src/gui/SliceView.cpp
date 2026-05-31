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
    constexpr uint8_t ALPHA = 115;
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

/*static*/ void SliceView::applyColormap(int cm, float t, int& r, int& g, int& b)
{
    t = std::max(0.f, std::min(1.f, t));
    switch (cm) {
    case HOT:
        r = std::min(255, (int)(t * 3 * 255));
        g = std::min(255, std::max(0, (int)((t*3-1)*255)));
        b = std::min(255, std::max(0, (int)((t*3-2)*255)));
        break;
    case COOL: {
        r = (int)(t * 255);
        g = (int)((1.f - t) * 255);
        b = 255;
        break;
    }
    case VIRIDIS: {
        // Approximation of matplotlib viridis
        float r4[4] = {0.267f, 0.128f, 0.369f, 0.993f};
        float g4[4] = {0.005f, 0.572f, 0.788f, 0.906f};
        float b4[4] = {0.329f, 0.553f, 0.392f, 0.144f};
        int seg = std::min(2, (int)(t * 3));
        float u = t * 3.f - seg;
        r = (int)((r4[seg]*(1-u)+r4[seg+1]*u)*255);
        g = (int)((g4[seg]*(1-u)+g4[seg+1]*u)*255);
        b = (int)((b4[seg]*(1-u)+b4[seg+1]*u)*255);
        break;
    }
    default: // GRAY
        r = g = b = (int)(t * 255);
        break;
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────

SliceView::SliceView(int axis, QWidget* parent)
    : QWidget(parent), m_axis(axis)
{
    setMinimumSize(200, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_labelVis.fill(true);
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
    resetZoom();
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

void SliceView::resetZoom()
{
    m_scale = 1.0; m_panX = 0; m_panY = 0;
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
    double sc = std::min(sx, sy) * m_scale;
    int dw = (int)(iw*sc), dh = (int)(ih*sc);
    int ox = (width()-dw)/2  + m_panX;
    int oy = (height()-dh)/2 + m_panY;
    return {ox, oy, dw, dh};
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
    m_vol->getImageSliceProj(m_axis, m_sliceIdx, m_projMode, m_slab, m_intensityBuf);
    m_vol->getMaskSlice     (m_axis, m_sliceIdx, m_maskBuf);

    float vmin  = m_vol->vmin(), vmax = m_vol->vmax();
    float range = vmax - vmin;
    if (range < 1e-6f) range = 1e-6f;

    // ── Extract aligned overlay slices (each on the REF grid) ──────────────────
    int NX = m_vol->nx(), NY = m_vol->ny(), NZ = m_vol->nz();
    struct OvSlice { const std::vector<float>* data; int cm; float a, wmin, wrange; };
    std::vector<std::vector<float>> ovBufs;
    std::vector<OvSlice>            ovSlices;
    ovBufs.reserve(m_overlays.size());
    for (const auto& ov : m_overlays) {
        if (!ov.arr) continue;
        std::vector<float> s;
        ROIVolume::sliceFromBuffer(ov.arr->GetBufferPointer(),
                                   NX, NY, NZ, m_axis, m_sliceIdx, s);
        if (static_cast<int>(s.size()) != rows*cols) continue;
        ovBufs.push_back(std::move(s));
        float wr = ov.wmax - ov.wmin; if (wr < 1e-6f) wr = 1e-6f;
        ovSlices.push_back({ &ovBufs.back(), ov.colormap, ov.alpha, ov.wmin, wr });
    }

    // Build RGB32 image: base → overlays → mask
    QImage img(cols, rows, QImage::Format_RGB32);
    for (int row = 0; row < rows; ++row) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(row));
        for (int col = 0; col < cols; ++col) {
            int ir, ig, ib;
            if (m_baseVisible) {
                float t = (m_intensityBuf[row*cols+col] - vmin) / range;
                applyColormap(m_colormap, t, ir, ig, ib);
            } else {
                ir = ig = ib = 0;
            }

            // ── Fusion overlays — intensity-modulated alpha (PET-on-CT look) ──
            for (const auto& ov : ovSlices) {
                float on = ((*ov.data)[row*cols+col] - ov.wmin) / ov.wrange;
                if (on <= 0.f) continue;
                if (on > 1.f)  on = 1.f;
                int orr, ogg, obb;
                applyColormap(ov.cm, on, orr, ogg, obb);
                float eff = ov.a * on;
                ir = (int)(ir*(1-eff) + orr*eff);
                ig = (int)(ig*(1-eff) + ogg*eff);
                ib = (int)(ib*(1-eff) + obb*eff);
            }

            // ── Mask label overlay (topmost) ─────────────────────────────────
            int16_t lbl = m_maskBuf[row*cols+col];
            if (lbl > 0 && lbl < static_cast<int16_t>(m_labelVis.size()) && m_labelVis[lbl]) {
                auto [lr,lg,lb,la] = labelColor(lbl);
                float a = (la/255.f) * m_overlayAlpha;
                ir = std::min(255,(int)(ir*(1-a)+lr*a));
                ig = std::min(255,(int)(ig*(1-a)+lg*a));
                ib = std::min(255,(int)(ib*(1-a)+lb*a));
            }
            line[col] = qRgb(std::min(255,ir), std::min(255,ig), std::min(255,ib));
        }
    }

    QRect ir = imageRect();
    Qt::TransformationMode tm = m_interpolate
        ? Qt::SmoothTransformation
        : Qt::FastTransformation;
    painter.drawImage(ir, img.scaled(ir.width(), ir.height(), Qt::IgnoreAspectRatio, tm));

    // Crosshair (hidden in projection mode — no single slice position)
    if (m_projMode == 0) {
        float scX = (float)ir.width()  / cols;
        float scY = (float)ir.height() / rows;
        int crossRow = (int)(m_crossA * scY) + ir.top();
        int crossCol = (int)(m_crossB * scX) + ir.left();
        QPen chPen(QColor(255,230,0,180), 1, Qt::DashLine);
        painter.setPen(chPen);
        painter.drawLine(ir.left(),  crossRow, ir.right(),  crossRow);
        painter.drawLine(crossCol,   ir.top(), crossCol,    ir.bottom());
    }

    // Colorbar legend
    if (m_showColorbar) {
        int barW = 10, barH = std::min(120, height() - 60);
        int bx = width() - barW - 6, by = (height() - barH) / 2;
        for (int i = 0; i < barH; ++i) {
            float t = 1.0f - (float)i / std::max(1, barH - 1);
            int r, g, b; applyColormap(m_colormap, t, r, g, b);
            painter.setPen(QColor(r, g, b));
            painter.drawLine(bx, by + i, bx + barW, by + i);
        }
        painter.setPen(QColor(60,60,60));
        painter.drawRect(bx, by, barW, barH);
        QFont cf = font(); cf.setPointSize(7); painter.setFont(cf);
        painter.setPen(QColor(210,210,210));
        painter.drawText(bx - 30, by + 8,    QString::number(vmax, 'f', 0));
        painter.drawText(bx - 30, by + barH, QString::number(vmin, 'f', 0));
    }

    // Projection tag
    if (m_projMode) {
        painter.setPen(QColor(255,200,90));
        QFont pf = font(); pf.setPointSize(8); pf.setBold(true); painter.setFont(pf);
        QString tag = (m_projMode == 1) ? "MIP" : "MinIP";
        if (m_slab) tag += QString(" ±%1").arg(m_slab);
        painter.drawText(ir.adjusted(4,18,0,0), Qt::AlignTop|Qt::AlignLeft, tag);
    }

    // Panel name + slice index
    static const char* NAMES[] = {"Sagittal","Coronal","Axial"};
    QFont f = font(); f.setPointSize(8); painter.setFont(f);
    painter.setPen(QColor(200,200,200));
    painter.drawText(ir.adjusted(4,4,0,0), Qt::AlignTop|Qt::AlignLeft,
                     QString("%1  [%2]  %3×").arg(NAMES[m_axis]).arg(m_sliceIdx)
                         .arg(m_scale, 0, 'f', 1));

    // Anatomical orientation labels
    if (m_vol) {
        auto labels = m_vol->sliceOrientLabels(m_axis);
        // labels = {top, bottom, left, right}
        painter.setPen(QColor(255,220,100));
        QFont of = font(); of.setPointSize(9); of.setBold(true);
        painter.setFont(of);
        if (!labels[0].empty())
            painter.drawText(ir.adjusted(0,4,0,0), Qt::AlignTop|Qt::AlignHCenter,
                             QString::fromStdString(labels[0]));
        if (!labels[1].empty())
            painter.drawText(ir.adjusted(0,0,0,-4), Qt::AlignBottom|Qt::AlignHCenter,
                             QString::fromStdString(labels[1]));
        if (!labels[2].empty())
            painter.drawText(ir.adjusted(4,0,0,0), Qt::AlignVCenter|Qt::AlignLeft,
                             QString::fromStdString(labels[2]));
        if (!labels[3].empty())
            painter.drawText(ir.adjusted(0,0,-4,0), Qt::AlignVCenter|Qt::AlignRight,
                             QString::fromStdString(labels[3]));
    }

    // Measurement overlay (always on top)
    paintMeasurements(painter);

    // ── Info overlay — W/L + slice position ───────────────────────────────────
    if (m_showInfoOverlay) {
        QFont inf = font(); inf.setPointSize(8); painter.setFont(inf);
        painter.setPen(QColor(210, 210, 210));

        // Bottom-right: window width and level
        float ww = m_vol->vmax() - m_vol->vmin();
        float wl = (m_vol->vmin() + m_vol->vmax()) * 0.5f;
        painter.drawText(ir.adjusted(0, 0, -4, -4),
                         Qt::AlignBottom | Qt::AlignRight,
                         QString("W:%1  L:%2").arg((int)ww).arg((int)wl));

        // Bottom-left: slice number and physical position
        int tot = (m_axis==0) ? m_vol->nx()
                              : (m_axis==1) ? m_vol->ny() : m_vol->nz();
        float posMm = (float)m_sliceIdx * (float)m_vol->voxelSpacingMm();
        painter.drawText(ir.adjusted(4, 0, 0, -4),
                         Qt::AlignBottom | Qt::AlignLeft,
                         QString("%1/%2  •  %3 mm")
                             .arg(m_sliceIdx + 1).arg(tot)
                             .arg(posMm, 0, 'f', 1));
    }
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
    if      (m_axis==0) { vx=m_sliceIdx; vy=row; vz=col; }
    else if (m_axis==1) { vx=row; vy=m_sliceIdx; vz=col; }
    else                { vx=row; vy=col;  vz=m_sliceIdx; }
    return true;
}

void SliceView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_measureMode != MeasureMode::None) {
        // ── Measurement click ─────────────────────────────────────────────────
        QRect ir = imageRect();
        if (ir.contains(e->pos())) {
            MeasurePt pt = { float(e->pos().x() - ir.x()) / ir.width(),
                             float(e->pos().y() - ir.y()) / ir.height() };
            pt.u = std::clamp(pt.u, 0.f, 1.f);
            pt.v = std::clamp(pt.v, 0.f, 1.f);

            if (m_measureMode == MeasureMode::Circle) {
                // Click+drag: press starts it
                m_pending = Measurement{m_measureMode, m_sliceIdx, {pt}, false, {}};
                m_measPending = true;
            } else {
                // Click-to-place (Ruler/Angle)
                if (!m_measPending) {
                    m_pending = Measurement{m_measureMode, m_sliceIdx, {pt}, false, {}};
                    m_measPending = true;
                } else {
                    m_pending.pts.push_back(pt);
                    int needed = (m_measureMode==MeasureMode::Ruler) ? 2 : 3;
                    if ((int)m_pending.pts.size() >= needed) {
                        m_pending.complete = true;
                        computeMeasureValue(m_pending);
                        m_measurements[m_sliceIdx].push_back(m_pending);
                        m_measPending = false;
                        emit measurementAdded(m_pending.valueStr);
                    }
                }
            }
            update();
        }
        QWidget::mousePressEvent(e);
        return;
    }

    if (e->button() == Qt::LeftButton) {
        m_leftDragging = true;
        int vx,vy,vz;
        if (widgetToVoxel(e->pos().x(), e->pos().y(), vx,vy,vz))
            emit sliceClicked(vx,vy,vz);
    } else if (e->button() == Qt::MiddleButton) {
        m_midDragging = true;
        m_midStartX = e->pos().x();
        m_midStartY = e->pos().y();
        m_panStartX = m_panX;
        m_panStartY = m_panY;
    } else if (e->button() == Qt::RightButton) {
        m_rightDragging = true;
        m_rightStartX = e->pos().x();
        m_rightStartY = e->pos().y();
        if (m_vol) {
            m_dragStartVmin = m_vol->vmin();
            m_dragStartVmax = m_vol->vmax();
        }
    }
    QWidget::mousePressEvent(e);
}

void SliceView::mouseMoveEvent(QMouseEvent* e)
{
    // Update live measurement cursor
    if (m_measureMode != MeasureMode::None) {
        QRect ir = imageRect();
        m_measCursor = {
            std::clamp(float(e->pos().x()-ir.x())/std::max(1,ir.width()),  0.f, 1.f),
            std::clamp(float(e->pos().y()-ir.y())/std::max(1,ir.height()), 0.f, 1.f)
        };
        if (m_measPending && m_measureMode == MeasureMode::Circle &&
            m_pending.pts.size() == 1) {
            // Drag updates second point of circle
            m_pending.pts.resize(2);
            m_pending.pts[1] = m_measCursor;
        }
        update();
    }

    if (m_leftDragging) {
        int vx,vy,vz;
        if (widgetToVoxel(e->pos().x(), e->pos().y(), vx,vy,vz))
            emit sliceDragged(vx,vy,vz);
    }
    if (m_midDragging) {
        m_panX = m_panStartX + (e->pos().x() - m_midStartX);
        m_panY = m_panStartY + (e->pos().y() - m_midStartY);
        update();
    }
    if (m_rightDragging && m_vol) {
        // Right drag: horizontal = window width, vertical = window centre
        float range = m_dragStartVmax - m_dragStartVmin;
        float centre = (m_dragStartVmax + m_dragStartVmin) * 0.5f;
        float dx = (e->pos().x() - m_rightStartX);
        float dy = (e->pos().y() - m_rightStartY);
        // dx widens/narrows window; dy shifts centre
        float newRange  = std::max(1.f, range  + dx * range * 0.005f);
        float newCentre = centre + dy * range * 0.005f;
        m_vol->setWindow(newCentre - newRange*0.5f, newCentre + newRange*0.5f);
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void SliceView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button()==Qt::LeftButton && m_measureMode == MeasureMode::Circle
        && m_measPending && m_pending.pts.size() >= 2) {
        // Complete the circle on release
        m_pending.complete = true;
        computeMeasureValue(m_pending);
        m_measurements[m_sliceIdx].push_back(m_pending);
        m_measPending = false;
        emit measurementAdded(m_pending.valueStr);
        update();
        m_leftDragging = false;   // always clear on any left-button release
        QWidget::mouseReleaseEvent(e);
        return;
    }
    if (e->button()==Qt::LeftButton) {
        m_leftDragging = false;   // always clear; prevents spurious sliceDragged after mode switch
        if (m_measureMode == MeasureMode::None) emit sliceReleased();
    }
    if (e->button()==Qt::MiddleButton) m_midDragging   = false;
    if (e->button()==Qt::RightButton)  m_rightDragging = false;
    QWidget::mouseReleaseEvent(e);
}

void SliceView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        // Ctrl+scroll = zoom in/out
        double factor = (e->angleDelta().y() > 0) ? 1.15 : (1.0/1.15);
        m_scale = std::max(0.25, std::min(16.0, m_scale * factor));
        update();
    } else {
        emit scrolled(e->angleDelta().y() > 0 ? 1 : -1);
    }
    e->accept();
}

// ── Measurement helpers ───────────────────────────────────────────────────────

void SliceView::setMeasureMode(MeasureMode mode)
{
    m_measureMode = mode;
    m_measPending = false;
    update();
}

void SliceView::clearMeasurements()
{
    m_measurements.clear();
    m_measPending = false;
    update();
}

const std::vector<SliceView::Measurement>& SliceView::measurementsForSlice(int idx) const
{
    static const std::vector<Measurement> empty;
    auto it = m_measurements.find(idx);
    return (it != m_measurements.end()) ? it->second : empty;
}

QPointF SliceView::widgetToNorm(QPointF w) const
{
    QRect ir = imageRect();
    return { (w.x()-ir.x()) / std::max(1,ir.width()),
             (w.y()-ir.y()) / std::max(1,ir.height()) };
}

QPointF SliceView::normToWidget(QPointF n) const
{
    QRect ir = imageRect();
    return { ir.x() + n.x()*ir.width(), ir.y() + n.y()*ir.height() };
}

float SliceView::normDistMm(MeasurePt a, MeasurePt b) const
{
    if (!m_vol || !m_vol->isLoaded()) return 0.f;
    float du = (b.u - a.u) * m_vol->sliceCols(m_axis);
    float dv = (b.v - a.v) * m_vol->sliceRows(m_axis);
    return std::sqrt(du*du + dv*dv) * (float)m_vol->voxelSpacingMm();
}

void SliceView::computeMeasureValue(Measurement& m) const
{
    if (m.type == MeasureMode::Ruler && m.pts.size() >= 2) {
        float d = normDistMm(m.pts[0], m.pts[1]);
        m.valueStr = QString("%1 mm").arg(d, 0, 'f', 1);
    }
    else if (m.type == MeasureMode::Angle && m.pts.size() >= 3) {
        // Angle at pts[1] (vertex)
        auto& A = m.pts[0]; auto& V = m.pts[1]; auto& B = m.pts[2];
        float ax = A.u-V.u, ay = A.v-V.v;
        float bx = B.u-V.u, by = B.v-V.v;
        float dot = ax*bx + ay*by;
        float la  = std::sqrt(ax*ax+ay*ay);
        float lb  = std::sqrt(bx*bx+by*by);
        float ang = (la*lb < 1e-9f) ? 0.f
                    : std::acos(std::clamp(dot/(la*lb), -1.f, 1.f))
                      * 180.f / 3.14159265f;
        m.valueStr = QString("%1°").arg(ang, 0, 'f', 1);
    }
    else if (m.type == MeasureMode::Circle && m.pts.size() >= 2) {
        float r = normDistMm(m.pts[0], m.pts[1]);
        float area = 3.14159265f * r * r;
        m.valueStr = QString("r=%1 mm  A=%2 mm²").arg(r,0,'f',1).arg(area,0,'f',1);
    }
}

void SliceView::paintMeasurements(QPainter& p) const
{
    const auto drawOne = [&](const Measurement& m, bool pending) {
        if (m.pts.empty()) return;

        QPen pen(QColor(255, 220, 40), 1.5);
        if (pending) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        // Collect widget-space points
        std::vector<QPointF> wpts;
        wpts.reserve(m.pts.size());
        for (auto& pt : m.pts)
            wpts.push_back(normToWidget({pt.u, pt.v}));

        // Add live cursor as last point if pending and incomplete
        QPointF cursorW = normToWidget({m_measCursor.u, m_measCursor.v});

        if (m.type == MeasureMode::Ruler) {
            QPointF p2 = (wpts.size()>=2) ? wpts[1] : cursorW;
            p.drawLine(wpts[0], p2);
            // Midpoint label
            QPointF mid = (wpts[0]+p2)*0.5;
            QString lbl = m.complete ? m.valueStr : "…";
            p.setPen(QColor(255,220,40));
            QFont f = p.font(); f.setPointSize(8); p.setFont(f);
            p.drawText(mid + QPointF(4,-4), lbl);
        }
        else if (m.type == MeasureMode::Angle) {
            if (wpts.size() >= 2) p.drawLine(wpts[0], wpts[1]);
            if (wpts.size() >= 3) p.drawLine(wpts[1], wpts[2]);
            else if (wpts.size() == 2) {
                // Show line-to-cursor
                pen.setStyle(Qt::DotLine); p.setPen(pen);
                p.drawLine(wpts[1], cursorW);
            }
            if (m.complete && wpts.size()>=3) {
                QFont f = p.font(); f.setPointSize(8); p.setFont(f);
                p.setPen(QColor(255,220,40));
                p.drawText(wpts[1]+QPointF(6,-6), m.valueStr);
            }
        }
        else if (m.type == MeasureMode::Circle) {
            QPointF centre = wpts[0];
            QPointF edge   = (wpts.size()>=2) ? wpts[1] : cursorW;
            float   rx     = float(edge.x()-centre.x());
            float   ry     = float(edge.y()-centre.y());
            float   r      = std::sqrt(rx*rx+ry*ry);
            p.drawEllipse(centre, (double)r, (double)r);
            p.setPen(QColor(255,220,40));
            QFont f = p.font(); f.setPointSize(8); p.setFont(f);
            if (m.complete) p.drawText(centre+QPointF(r+4, 0), m.valueStr);
        }

        // Draw point handles
        p.setPen(QPen(QColor(255,100,100), 1));
        p.setBrush(QColor(255,100,100,180));
        for (auto& wp : wpts)
            p.drawEllipse(wp, 3.0, 3.0);
    };

    // Completed measurements for current slice
    auto it = m_measurements.find(m_sliceIdx);
    if (it != m_measurements.end())
        for (const auto& m : it->second) drawOne(m, false);

    // In-progress measurement
    if (m_measPending) drawOne(m_pending, true);
}
