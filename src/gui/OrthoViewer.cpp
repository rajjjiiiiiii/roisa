// OrthoViewer.cpp — Three-panel orthogonal viewer

#include "OrthoViewer.h"
#include "SliceView.h"

#include <QGridLayout>
#include <QLabel>
#include <algorithm>

OrthoViewer::OrthoViewer(QWidget* parent)
    : QWidget(parent)
{
    m_sagView = new SliceView(0, this);
    m_corView = new SliceView(1, this);
    m_axiView = new SliceView(2, this);

    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(2,2,2,2);
    layout->setSpacing(2);
    layout->addWidget(m_sagView, 0, 0);
    layout->addWidget(m_corView, 0, 1);
    layout->addWidget(m_axiView, 1, 0);

    auto* ph = new QLabel(this);
    ph->setStyleSheet("background:#111;");
    layout->addWidget(ph, 1, 1);

    // Connect each panel — track which axis was last active so MainWindow
    // can read it when computing the brush footprint.
    connect(m_sagView, &SliceView::sliceClicked,
            this, [this](int x,int y,int z){ m_activeAxis=0; onSliceClicked(x,y,z); });
    connect(m_corView, &SliceView::sliceClicked,
            this, [this](int x,int y,int z){ m_activeAxis=1; onSliceClicked(x,y,z); });
    connect(m_axiView, &SliceView::sliceClicked,
            this, [this](int x,int y,int z){ m_activeAxis=2; onSliceClicked(x,y,z); });

    connect(m_sagView, &SliceView::sliceDragged,
            this, [this](int x,int y,int z){ m_activeAxis=0; onSliceDragged(x,y,z); });
    connect(m_corView, &SliceView::sliceDragged,
            this, [this](int x,int y,int z){ m_activeAxis=1; onSliceDragged(x,y,z); });
    connect(m_axiView, &SliceView::sliceDragged,
            this, [this](int x,int y,int z){ m_activeAxis=2; onSliceDragged(x,y,z); });

    // Forward sliceReleased from every panel so MainWindow can commit stroke undo
    for (auto* sv : {m_sagView, m_corView, m_axiView})
        connect(sv, &SliceView::sliceReleased, this, &OrthoViewer::sliceReleased);

    connect(m_sagView, &SliceView::scrolled,
            this, [this](int d){ onSliceScrolled(0,d); });
    connect(m_corView, &SliceView::scrolled,
            this, [this](int d){ onSliceScrolled(1,d); });
    connect(m_axiView, &SliceView::scrolled,
            this, [this](int d){ onSliceScrolled(2,d); });
}

void OrthoViewer::setVolume(ROIVolume* vol)
{
    m_vol = vol;
    m_sagView->setVolume(vol);
    m_corView->setVolume(vol);
    m_axiView->setVolume(vol);

    if (vol && vol->isLoaded()) {
        m_x = vol->nx()/2;  m_y = vol->ny()/2;  m_z = vol->nz()/2;
        m_sagView->setSliceIndex(m_x);
        m_corView->setSliceIndex(m_y);
        m_axiView->setSliceIndex(m_z);
        updateCrosshairs();
    }
}

void OrthoViewer::setX(int x)
{
    m_x = x;
    m_sagView->setSliceIndex(x);
    updateCrosshairs();
}
void OrthoViewer::setY(int y)
{
    m_y = y;
    m_corView->setSliceIndex(y);
    updateCrosshairs();
}
void OrthoViewer::setZ(int z)
{
    m_z = z;
    m_axiView->setSliceIndex(z);
    updateCrosshairs();
}

void OrthoViewer::refresh()
{
    m_sagView->update();
    m_corView->update();
    m_axiView->update();
}

void OrthoViewer::updateCrosshairs()
{
    // Sagittal (x=m_x): rows=ny,cols=nz → a=y, b=z
    m_sagView->setCrosshair(m_y, m_z);
    // Coronal (y=m_y): rows=nx,cols=nz → a=x, b=z
    m_corView->setCrosshair(m_x, m_z);
    // Axial (z=m_z): rows=nx,cols=ny → a=x, b=y
    m_axiView->setCrosshair(m_x, m_y);
}

void OrthoViewer::onSliceClicked(int x, int y, int z)
{
    m_seedX=x; m_seedY=y; m_seedZ=z;
    emit seedSet(x,y,z);
    m_x=x; m_y=y; m_z=z;
    m_sagView->setSliceIndex(m_x);
    m_corView->setSliceIndex(m_y);
    m_axiView->setSliceIndex(m_z);
    updateCrosshairs();
    emit positionChanged(m_x, m_y, m_z);
}

void OrthoViewer::onSliceDragged(int x, int y, int z)
{
    // Continuously update seed during drag (for paint mode this is handled
    // by MainWindow; for segment mode the seed updates on drag too)
    m_seedX=x; m_seedY=y; m_seedZ=z;
    emit seedSet(x,y,z);
}

void OrthoViewer::onSliceScrolled(int viewerAxis, int delta)
{
    if (!m_vol) return;
    if      (viewerAxis==0) { m_x = std::max(0,std::min(m_vol->nx()-1,m_x+delta)); m_sagView->setSliceIndex(m_x); }
    else if (viewerAxis==1) { m_y = std::max(0,std::min(m_vol->ny()-1,m_y+delta)); m_corView->setSliceIndex(m_y); }
    else                    { m_z = std::max(0,std::min(m_vol->nz()-1,m_z+delta)); m_axiView->setSliceIndex(m_z); }
    updateCrosshairs();
    emit positionChanged(m_x, m_y, m_z);
}
