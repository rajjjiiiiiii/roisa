// MainWindow.cpp — Top-level application window

#include "MainWindow.h"
#include "OrthoViewer.h"
#include "ToolPanel.h"
#include "../core/ROIAlgorithms.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QStatusBar>

#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_vol(std::make_unique<ROIVolume>())
{
    setWindowTitle("ROISA — ROI Segmentation Assistant");
    resize(1200, 750);
    setStyleSheet(
        "QMainWindow{background:#1a1a1a;}"
        "QGroupBox{color:#ccc;}"
        "QLabel{color:#ccc;}"
        "QComboBox,QSpinBox,QDoubleSpinBox,QLineEdit{"
        "  background:#2a2a2a;color:#eee;border:1px solid #555;}");

    // ── Central: orthogonal viewer ─────────────────────────────────────────
    m_viewer = new OrthoViewer(this);
    setCentralWidget(m_viewer);

    // ── Right dock: tool panel ─────────────────────────────────────────────
    m_toolPanel = new ToolPanel(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(m_toolPanel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(300);
    scroll->setMaximumWidth(350);
    scroll->setStyleSheet("QScrollArea{border:none;background:#1a1a1a;}");

    auto* dock = new QDockWidget("Controls", this);
    dock->setWidget(scroll);
    dock->setAllowedAreas(Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // ── Connections ────────────────────────────────────────────────────────
    connect(m_viewer, &OrthoViewer::positionChanged,
            m_toolPanel, &ToolPanel::onPositionChanged);
    connect(m_viewer, &OrthoViewer::seedSet,
            m_toolPanel, &ToolPanel::onSeedSet);
    connect(m_viewer, &OrthoViewer::seedSet,
            this, &MainWindow::onSeedOrPaint);
    connect(m_toolPanel, &ToolPanel::refreshRequested,
            m_viewer, &OrthoViewer::refresh);
    connect(m_viewer, &OrthoViewer::sliceReleased,
            this, &MainWindow::onMouseReleased);

    // Mask change → repaint viewer
    m_vol->setChangeCallback([this]{ m_viewer->refresh(); });

    buildMenus();
    statusBar()->showMessage("Ready — File → Open Image or Open DICOM to load.");
}

// ── Menus ─────────────────────────────────────────────────────────────────────

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu("File");
    auto* openAct  = new QAction("Open Image…",  this);
    auto* dicomAct = new QAction("Open DICOM…",  this);
    auto* quitAct  = new QAction("Quit",          this);
    openAct->setShortcut(QKeySequence::Open);
    quitAct->setShortcut(QKeySequence::Quit);
    fileMenu->addAction(openAct);
    fileMenu->addAction(dicomAct);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAct);
    connect(openAct,  &QAction::triggered, this, &MainWindow::openImage);
    connect(dicomAct, &QAction::triggered, this, &MainWindow::openDicom);
    connect(quitAct,  &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu("View");
    auto* resetWin = new QAction("Reset Intensity Window", this);
    viewMenu->addAction(resetWin);
    connect(resetWin, &QAction::triggered, this, [this]{
        if (m_vol && m_vol->isLoaded()) m_viewer->refresh();
    });

    auto* helpMenu = menuBar()->addMenu("Help");
    auto* aboutAct = new QAction("About", this);
    helpMenu->addAction(aboutAct);
    connect(aboutAct, &QAction::triggered, this, [this]{
        QMessageBox::about(this, "About ROISA",
            "<b>ROISA</b> — ROI Segmentation Assistant v1.0<br>"
            "3D medical image ROI segmentation<br><br>"
            "Built with ITK + Qt");
    });
}

// ── Load ──────────────────────────────────────────────────────────────────────

void MainWindow::loadPath(const QString& path)
{
    statusBar()->showMessage("Loading " + path + " …");
    if (!m_vol->load(path.toStdString())) {
        QMessageBox::critical(this,"Load error","Failed to load:\n"+path);
        statusBar()->showMessage("Load failed.");
        return;
    }
    afterLoad();
}

void MainWindow::openImage()
{
    QString p = QFileDialog::getOpenFileName(this,"Open Image","",
        "NIfTI / MetaImage (*.nii *.nii.gz *.mhd *.mha *.nrrd);;All files (*)");
    if (!p.isEmpty()) loadPath(p);
}

void MainWindow::openDicom()
{
    QString d = QFileDialog::getExistingDirectory(this,"Select DICOM folder");
    if (!d.isEmpty()) loadPath(d);
}

void MainWindow::afterLoad()
{
    m_toolPanel->setVolume(m_vol.get());
    m_toolPanel->setViewer(m_viewer);
    m_viewer->setVolume(m_vol.get());
    statusBar()->showMessage(
        QString("Loaded: %1 × %2 × %3 voxels")
            .arg(m_vol->nx()).arg(m_vol->ny()).arg(m_vol->nz()));
}

// ── Brush footprint (mirrors Python _get_brush_indices) ───────────────────────

void MainWindow::brushFootprint(int cx, int cy, int cz,
                                 int radius, int shape,
                                 int viewAxis, bool twoD,
                                 std::vector<std::array<int,3>>& out) const
{
    if (!m_vol->isLoaded()) return;
    int NX=m_vol->nx(), NY=m_vol->ny(), NZ=m_vol->nz();
    int r = std::max(1, radius);
    int xr=(twoD&&viewAxis==0)?0:r;
    int yr=(twoD&&viewAxis==1)?0:r;
    int zr=(twoD&&viewAxis==2)?0:r;
    for (int dz=-zr;dz<=zr;++dz)
      for (int dy=-yr;dy<=yr;++dy)
        for (int dx=-xr;dx<=xr;++dx) {
            int x=cx+dx, y=cy+dy, z=cz+dz;
            if (x<0||x>=NX||y<0||y>=NY||z<0||z>=NZ) continue;
            bool hit=false;
            if      (shape==2) hit=true;
            else if (shape==1){
                if      (viewAxis==2) hit=dx*dx+dy*dy<=r*r;
                else if (viewAxis==1) hit=dx*dx+dz*dz<=r*r;
                else                  hit=dy*dy+dz*dz<=r*r;
            } else             hit=dx*dx+dy*dy+dz*dz<=r*r;
            if (hit) out.push_back({x,y,z});
        }
}

// ── Paint / erase on mouse events ────────────────────────────────────────────

void MainWindow::onSeedOrPaint(int x, int y, int z)
{
    if (!m_vol || !m_vol->isLoaded()) return;
    QString mode = m_toolPanel->toolMode();
    if (mode != "paint" && mode != "erase") return;

    int16_t label = (mode=="paint")
                    ? static_cast<int16_t>(m_toolPanel->activeLabel())
                    : 0;

    if (!m_inStroke) {
        m_inStroke = true;
        m_strokeFirst.clear();
    }

    // Read brush settings from ToolPanel
    int  radius = m_toolPanel->brushRadius();
    int  shape  = m_toolPanel->brushShape();
    int  axis   = m_viewer->activeAxis();
    bool twoD   = m_toolPanel->twoDOnly();

    std::vector<std::array<int,3>> fp;
    brushFootprint(x, y, z, radius, shape, axis, twoD, fp);

    int16_t* buf = m_vol->maskImage()->GetBufferPointer();
    int NX = m_vol->nx(), NY = m_vol->ny();
    for (auto& [vx,vy,vz] : fp) {
        int lin = vx + NX*vy + NX*NY*vz;
        if (m_strokeFirst.find(lin)==m_strokeFirst.end())
            m_strokeFirst[lin] = buf[lin];
        buf[lin] = label;
    }
    m_vol->notifyChange();
}

void MainWindow::onMouseReleased()
{
    if (!m_inStroke || m_strokeFirst.empty()) { m_inStroke=false; return; }
    // Commit entire stroke as one undo entry
    int NX=m_vol->nx(), NY=m_vol->ny();
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t>           olds;
    idxs.reserve(m_strokeFirst.size());
    olds.reserve(m_strokeFirst.size());
    for (auto& [lin,oldVal] : m_strokeFirst) {
        int z=lin/(NX*NY), y=(lin%(NX*NY))/NX, vx=lin%NX;
        idxs.push_back({vx,y,z}); olds.push_back(oldVal);
    }
    m_vol->pushUndo(std::move(idxs), std::move(olds));
    m_strokeFirst.clear();
    m_inStroke = false;
}
