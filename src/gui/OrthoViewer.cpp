// OrthoViewer.cpp — Multi-panel orthogonal viewer with switchable layout presets

#include "OrthoViewer.h"
#include "SliceView.h"
#include "VtkView.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QTimer>
#include <algorithm>

// ── Layout helper ─────────────────────────────────────────────────────────────

static void styleSplitter(QSplitter* s)
{
    s->setHandleWidth(3);
    s->setStyleSheet("QSplitter::handle{background:#2a2a2a;}");
}

// ── Construction ──────────────────────────────────────────────────────────────

OrthoViewer::OrthoViewer(QWidget* parent)
    : QWidget(parent)
{
    m_cineTimer = new QTimer(this);
    connect(m_cineTimer, &QTimer::timeout, this, &OrthoViewer::onCineTick);

    // Create the four panels (unparented for now — applyLayoutPreset parents them)
    m_vtkView = new VtkView(this);
    m_sagView = new SliceView(0, this);
    m_corView = new SliceView(1, this);
    m_axiView = new SliceView(2, this);

    // ── Outer layout: [toolbar] [view area] ──────────────────────────────────
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(2, 2, 2, 2);
    outer->setSpacing(2);

    // Preset toolbar
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(26);
    toolbar->setStyleSheet(
        "QWidget{background:#1a1a1a;}"
        "QPushButton{background:#252525;color:#aaa;border:1px solid #3a3a3a;"
        "  padding:1px 7px;font-size:11px;border-radius:2px;}"
        "QPushButton:checked{background:#1a3d5c;color:#7ec8ff;"
        "  border:1px solid #3080b0;}"
        "QPushButton:hover:!checked{background:#2e2e2e;}");

    auto* tbL = new QHBoxLayout(toolbar);
    tbL->setContentsMargins(3, 3, 3, 3);
    tbL->setSpacing(3);

    static const struct { const char* label; const char* tip; } PRESETS[6] = {
        {"2×2",  "Four equal panels  (VTK | Sag / Cor | Axi)"},
        {"1+3",  "Large Axial + three small panels  (Sag / Cor / VTK)"},
        {"3-up", "Three slices side-by-side  (Sag | Cor | Axi)"},
        {"Axi",  "Axial view only"},
        {"3D",   "3-D VTK view only"},
        {"1×4",  "All four panels in a row  (Sag | Cor | Axi | 3D)"},
    };
    for (int i = 0; i < 6; ++i) {
        auto* btn = new QPushButton(PRESETS[i].label, toolbar);
        btn->setToolTip(PRESETS[i].tip);
        btn->setCheckable(true);
        btn->setFixedHeight(20);
        m_presetBtns[i] = btn;
        tbL->addWidget(btn);
        connect(btn, &QPushButton::clicked, this,
                [this, i]{ applyLayoutPreset(i); });
    }
    tbL->addStretch();
    outer->addWidget(toolbar);

    // View container — hosts whichever QSplitter tree is active
    m_viewContainer = new QWidget(this);
    m_viewContainerLayout = new QVBoxLayout(m_viewContainer);
    m_viewContainerLayout->setContentsMargins(0, 0, 0, 0);
    m_viewContainerLayout->setSpacing(0);
    outer->addWidget(m_viewContainer, 1);

    // ── Signal wiring ─────────────────────────────────────────────────────────
    connect(m_sagView, &SliceView::sliceClicked,
            this, [this](int x, int y, int z){ m_activeAxis = 0; onSliceClicked(x, y, z); });
    connect(m_corView, &SliceView::sliceClicked,
            this, [this](int x, int y, int z){ m_activeAxis = 1; onSliceClicked(x, y, z); });
    connect(m_axiView, &SliceView::sliceClicked,
            this, [this](int x, int y, int z){ m_activeAxis = 2; onSliceClicked(x, y, z); });

    connect(m_sagView, &SliceView::sliceDragged,
            this, [this](int x, int y, int z){ m_activeAxis = 0; onSliceDragged(x, y, z); });
    connect(m_corView, &SliceView::sliceDragged,
            this, [this](int x, int y, int z){ m_activeAxis = 1; onSliceDragged(x, y, z); });
    connect(m_axiView, &SliceView::sliceDragged,
            this, [this](int x, int y, int z){ m_activeAxis = 2; onSliceDragged(x, y, z); });

    for (auto* sv : {m_sagView, m_corView, m_axiView})
        connect(sv, &SliceView::sliceReleased, this, &OrthoViewer::sliceReleased);

    connect(m_sagView, &SliceView::scrolled, this, [this](int d){ onSliceScrolled(0, d); });
    connect(m_corView, &SliceView::scrolled, this, [this](int d){ onSliceScrolled(1, d); });
    connect(m_axiView, &SliceView::scrolled, this, [this](int d){ onSliceScrolled(2, d); });

    for (auto* sv : {m_sagView, m_corView, m_axiView}) {
        connect(sv, &SliceView::measurementAdded, this, &OrthoViewer::measurementAdded);
        connect(sv, &SliceView::polygonClosed,    this, &OrthoViewer::polygonClosed);
    }

    // Apply the default layout
    applyLayoutPreset(0);
}

// ── Layout presets ────────────────────────────────────────────────────────────

void OrthoViewer::applyLayoutPreset(int preset)
{
    // Step 1: detach all four views from whatever splitter tree they're in now
    for (auto* w : {(QWidget*)m_vtkView, (QWidget*)m_sagView,
                    (QWidget*)m_corView,  (QWidget*)m_axiView}) {
        w->setParent(m_viewContainer);
        w->hide();
    }

    // Step 2: destroy the old splitter tree (views are safe — already detached)
    if (m_currentSplitter) {
        m_viewContainerLayout->removeWidget(m_currentSplitter);
        delete m_currentSplitter;
        m_currentSplitter = nullptr;
    }

    QSplitter* root = nullptr;

    switch (preset) {
    default: preset = 0; [[fallthrough]];
    case 0: {
        // 2×2 — four equal quadrants with resizable dividers
        root         = new QSplitter(Qt::Horizontal);
        auto* leftV  = new QSplitter(Qt::Vertical);
        auto* rightV = new QSplitter(Qt::Vertical);
        leftV->addWidget(m_vtkView);  leftV->addWidget(m_corView);
        rightV->addWidget(m_sagView); rightV->addWidget(m_axiView);
        root->addWidget(leftV); root->addWidget(rightV);
        styleSplitter(root); styleSplitter(leftV); styleSplitter(rightV);
        break;
    }
    case 1: {
        // 1+3 — large Axial (≈70 %) on the left, three stacked on the right
        root         = new QSplitter(Qt::Horizontal);
        auto* rightV = new QSplitter(Qt::Vertical);
        rightV->addWidget(m_sagView);
        rightV->addWidget(m_corView);
        rightV->addWidget(m_vtkView);
        root->addWidget(m_axiView);
        root->addWidget(rightV);
        styleSplitter(root); styleSplitter(rightV);
        // Defer setSizes until the widget has a size (use a single-shot timer)
        QTimer::singleShot(0, root, [root]{
            int w = root->width();
            if (w > 0) root->setSizes({w * 7 / 10, w * 3 / 10});
        });
        break;
    }
    case 2: {
        // 3-up — three slices side by side; VTK stays hidden
        root = new QSplitter(Qt::Horizontal);
        root->addWidget(m_sagView);
        root->addWidget(m_corView);
        root->addWidget(m_axiView);
        styleSplitter(root);
        break;
    }
    case 3: {
        // Axial only
        root = new QSplitter(Qt::Horizontal);
        root->addWidget(m_axiView);
        styleSplitter(root);
        break;
    }
    case 4: {
        // 3D only
        root = new QSplitter(Qt::Horizontal);
        root->addWidget(m_vtkView);
        styleSplitter(root);
        break;
    }
    case 5: {
        // 1×4 — all four panels in a row: Sag | Cor | Axi | 3D
        root = new QSplitter(Qt::Horizontal);
        root->addWidget(m_sagView);
        root->addWidget(m_corView);
        root->addWidget(m_axiView);
        root->addWidget(m_vtkView);
        styleSplitter(root);
        break;
    }
    }

    m_currentSplitter = root;
    m_viewContainerLayout->addWidget(root);

    // Update toolbar button states
    for (int i = 0; i < 6; ++i)
        if (m_presetBtns[i]) m_presetBtns[i]->setChecked(i == preset);

    m_currentPreset = preset;
}

void OrthoViewer::setLayoutPreset(int preset)
{
    applyLayoutPreset(preset);
}

// ── Volume assignment ─────────────────────────────────────────────────────────

void OrthoViewer::setVolume(ROIVolume* vol)
{
    m_vol = vol;
    m_sagView->setVolume(vol);
    m_corView->setVolume(vol);
    m_axiView->setVolume(vol);
    m_vtkView->setVolume(vol);     // starts VTK pipeline; renders immediately

    if (vol && vol->isLoaded()) {
        m_x = vol->nx() / 2;
        m_y = vol->ny() / 2;
        m_z = vol->nz() / 2;
        m_sagView->setSliceIndex(m_x);
        m_corView->setSliceIndex(m_y);
        m_axiView->setSliceIndex(m_z);
        updateCrosshairs();
    }
}

// ── Navigation ────────────────────────────────────────────────────────────────

void OrthoViewer::setX(int x) { m_x = x; m_sagView->setSliceIndex(x); updateCrosshairs(); }
void OrthoViewer::setY(int y) { m_y = y; m_corView->setSliceIndex(y); updateCrosshairs(); }
void OrthoViewer::setZ(int z) { m_z = z; m_axiView->setSliceIndex(z); updateCrosshairs(); }

// ── Refresh ───────────────────────────────────────────────────────────────────

void OrthoViewer::refresh()
{
    m_sagView->update();
    m_corView->update();
    m_axiView->update();
}

// ── VTK panel controls ────────────────────────────────────────────────────────

void OrthoViewer::refreshSurface(int label)
{
    m_vtkView->refreshSurface(label);
}

void OrthoViewer::refreshVtk()
{
    m_vtkView->refreshVolume();
    m_vtkView->refreshSurface(m_vtkView->renderMode() >= 1 ? -1 : -2);
}

void OrthoViewer::setVtkRenderMode(int mode)
{
    m_vtkView->setRenderMode(mode);
}

void OrthoViewer::resetVtkCamera()
{
    m_vtkView->resetCamera();
}

// ── Slice display options ─────────────────────────────────────────────────────

void OrthoViewer::setInterpolate(bool on)
{
    m_sagView->setInterpolate(on);
    m_corView->setInterpolate(on);
    m_axiView->setInterpolate(on);
}

void OrthoViewer::setColormap(int cm)
{
    m_sagView->setColormap(cm);
    m_corView->setColormap(cm);
    m_axiView->setColormap(cm);
}

void OrthoViewer::setOverlayAlpha(float a)
{
    m_sagView->setOverlayAlpha(a);
    m_corView->setOverlayAlpha(a);
    m_axiView->setOverlayAlpha(a);
}

void OrthoViewer::setOverlays(const std::vector<SliceView::FusionLayer>& ov)
{
    m_sagView->setOverlays(ov);
    m_corView->setOverlays(ov);
    m_axiView->setOverlays(ov);
}

void OrthoViewer::setBaseVisible(bool on)
{
    m_sagView->setBaseVisible(on);
    m_corView->setBaseVisible(on);
    m_axiView->setBaseVisible(on);
}

void OrthoViewer::setBaseColormap(int cm)
{
    m_sagView->setColormap(cm);
    m_corView->setColormap(cm);
    m_axiView->setColormap(cm);
}

int OrthoViewer::baseColormap() const
{
    return m_axiView->colormap();
}

void OrthoViewer::setAllLabelsVisible(bool v)
{
    m_sagView->setAllLabelsVisible(v);
    m_corView->setAllLabelsVisible(v);
    m_axiView->setAllLabelsVisible(v);
}

void OrthoViewer::setProjectionMode(int mode)
{
    m_sagView->setProjectionMode(mode);
    m_corView->setProjectionMode(mode);
    m_axiView->setProjectionMode(mode);
}

void OrthoViewer::setSlab(int slab)
{
    m_sagView->setSlab(slab);
    m_corView->setSlab(slab);
    m_axiView->setSlab(slab);
}

void OrthoViewer::setShowColorbar(bool on)
{
    m_sagView->setShowColorbar(on);
    m_corView->setShowColorbar(on);
    m_axiView->setShowColorbar(on);
}

void OrthoViewer::setVtkClip(bool enabled, int axis, double frac)
{
    m_vtkView->setClip(enabled, axis, frac);
}

void OrthoViewer::setPreviewBuffer(const uint8_t* buf)
{
    m_sagView->setPreviewBuffer(buf);
    m_corView->setPreviewBuffer(buf);
    m_axiView->setPreviewBuffer(buf);
}

void OrthoViewer::setPolygonMode(bool on)
{
    m_sagView->setPolygonMode(on);
    m_corView->setPolygonMode(on);
    m_axiView->setPolygonMode(on);
}

QStringList OrthoViewer::measurements() const
{
    QStringList out;
    const std::pair<const char*, SliceView*> views[] = {
        {"Sagittal", m_sagView}, {"Coronal", m_corView}, {"Axial", m_axiView}};
    for (const auto& v : views)
        for (const auto& m : v.second->measurements())
            out << (QString(v.first) + ": " + m);
    return out;
}

void OrthoViewer::setMeasureMode(int mode)
{
    auto m = static_cast<SliceView::MeasureMode>(mode);
    m_sagView->setMeasureMode(m);
    m_corView->setMeasureMode(m);
    m_axiView->setMeasureMode(m);
}

void OrthoViewer::clearMeasurements()
{
    m_sagView->clearMeasurements();
    m_corView->clearMeasurements();
    m_axiView->clearMeasurements();
}

void OrthoViewer::resetAllZoom()
{
    m_sagView->resetZoom();
    m_corView->resetZoom();
    m_axiView->resetZoom();
}

// ── Crosshair sync ────────────────────────────────────────────────────────────

void OrthoViewer::updateCrosshairs()
{
    m_sagView->setCrosshair(m_y, m_z);
    m_corView->setCrosshair(m_x, m_z);
    m_axiView->setCrosshair(m_x, m_y);
}

// ── Slice event handlers ──────────────────────────────────────────────────────

void OrthoViewer::onSliceClicked(int x, int y, int z)
{
    m_seedX = x; m_seedY = y; m_seedZ = z;
    emit seedSet(x, y, z);
    m_x = x; m_y = y; m_z = z;
    m_sagView->setSliceIndex(m_x);
    m_corView->setSliceIndex(m_y);
    m_axiView->setSliceIndex(m_z);
    updateCrosshairs();
    emit positionChanged(m_x, m_y, m_z);
}

void OrthoViewer::onSliceDragged(int x, int y, int z)
{
    m_seedX = x; m_seedY = y; m_seedZ = z;
    emit seedSet(x, y, z);
}

void OrthoViewer::onSliceScrolled(int viewerAxis, int delta)
{
    if (!m_vol) return;
    if      (viewerAxis == 0) { m_x = std::clamp(m_x + delta, 0, m_vol->nx()-1); m_sagView->setSliceIndex(m_x); }
    else if (viewerAxis == 1) { m_y = std::clamp(m_y + delta, 0, m_vol->ny()-1); m_corView->setSliceIndex(m_y); }
    else                      { m_z = std::clamp(m_z + delta, 0, m_vol->nz()-1); m_axiView->setSliceIndex(m_z); }
    updateCrosshairs();
    emit positionChanged(m_x, m_y, m_z);
}

// ── Info overlay passthrough ──────────────────────────────────────────────────

void OrthoViewer::setShowInfoOverlay(bool on)
{
    m_sagView->setShowInfoOverlay(on);
    m_corView->setShowInfoOverlay(on);
    m_axiView->setShowInfoOverlay(on);
}

// ── Cine player ───────────────────────────────────────────────────────────────

void OrthoViewer::playCine()
{
    if (!m_vol || !m_vol->isLoaded()) return;
    m_cinePlaying = true;
    m_cineTimer->start(1000 / std::max(1, m_cineFps));
}

void OrthoViewer::stopCine()
{
    m_cineTimer->stop();
    m_cinePlaying = false;
}

void OrthoViewer::setCineAxis(int axis)
{
    m_cineAxis = std::clamp(axis, 0, 2);
}

void OrthoViewer::setCineFps(int fps)
{
    m_cineFps = std::clamp(fps, 1, 30);
    if (m_cineTimer->isActive())
        m_cineTimer->setInterval(1000 / m_cineFps);
}

// ── Panel snapshots ───────────────────────────────────────────────────────────

QPixmap OrthoViewer::grabSagittal() const { return m_sagView->grab(); }
QPixmap OrthoViewer::grabCoronal()  const { return m_corView->grab(); }
QPixmap OrthoViewer::grabAxial()    const { return m_axiView->grab(); }
QPixmap OrthoViewer::grabVtk()      const { return m_vtkView->grab(); }

void OrthoViewer::onCineTick()
{
    if (!m_vol || !m_vol->isLoaded()) { stopCine(); return; }
    if (m_cineAxis == 0) {
        m_x = (m_x + 1) % m_vol->nx();
        m_sagView->setSliceIndex(m_x);
    } else if (m_cineAxis == 1) {
        m_y = (m_y + 1) % m_vol->ny();
        m_corView->setSliceIndex(m_y);
    } else {
        m_z = (m_z + 1) % m_vol->nz();
        m_axiView->setSliceIndex(m_z);
    }
    updateCrosshairs();
    emit positionChanged(m_x, m_y, m_z);
}
