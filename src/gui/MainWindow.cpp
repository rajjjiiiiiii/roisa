// MainWindow.cpp — Top-level application window

#include "MainWindow.h"
#include "OrthoViewer.h"
#include "SeriesBrowser.h"
#include "ToolPanel.h"
#include "../core/ROIAlgorithms.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Seed with one empty reference volume
    m_volumes.push_back(std::make_unique<ROIVolume>());
    m_volVisible.push_back(true);
    m_volNames.push_back("(none)");

    setWindowTitle("ROISA — ROI Segmentation Assistant");
    resize(1280, 800);
    setStyleSheet(
        "QMainWindow{background:#1a1a1a;}"
        "QGroupBox{color:#ccc;}"
        "QLabel{color:#ccc;}"
        "QComboBox,QSpinBox,QDoubleSpinBox,QLineEdit{"
        "  background:#2a2a2a;color:#eee;border:1px solid #555;}"
        "QTableWidget{background:#222;color:#ddd;gridline-color:#444;}"
        "QHeaderView::section{background:#2a2a2a;color:#aaa;border:1px solid #444;}");

    // ── Central: orthogonal viewer ─────────────────────────────────────────
    m_viewer = new OrthoViewer(this);
    setCentralWidget(m_viewer);

    // ── Left dock: series / file browser ──────────────────────────────────
    m_browser = new SeriesBrowser(this);
    auto* browserDock = new QDockWidget("Series Browser", this);
    browserDock->setWidget(m_browser);
    browserDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    browserDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, browserDock);

    connect(m_browser, &SeriesBrowser::dicomSeriesRequested,
            this, &MainWindow::loadDicomSeries);
    connect(m_browser, &SeriesBrowser::fileRequested,
            this, &MainWindow::loadPath);

    // ── Right dock: image list (header) + tool panel ───────────────────────
    m_toolPanel = new ToolPanel(this);
    m_imageList = new ImageListWidget(this);

    // Seed image list with the empty reference placeholder
    m_imageList->addImage("(none)", /*isRef=*/true);
    m_imageList->setActive(0);
    m_imageList->setRemoveEnabled(0, false);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(m_toolPanel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea{border:none;background:#1a1a1a;}");

    // Container: image list on top, panel below
    auto* dockBody   = new QWidget(this);
    auto* dockLayout = new QVBoxLayout(dockBody);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    dockLayout->setSpacing(0);
    dockLayout->addWidget(m_imageList);
    dockLayout->addWidget(scroll, 1);
    dockBody->setMinimumWidth(310);
    dockBody->setMaximumWidth(360);

    auto* dock = new QDockWidget("Controls", this);
    dock->setWidget(dockBody);
    dock->setAllowedAreas(Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    // ── ImageListWidget signals ────────────────────────────────────────────
    connect(m_imageList, &ImageListWidget::activateRequested,
            this, &MainWindow::activateImage);

    connect(m_imageList, &ImageListWidget::visibilityToggled, this,
            [this](int idx, bool on) {
                if (idx < 0 || idx >= (int)m_volVisible.size()) return;
                m_volVisible[idx] = on;
                m_imageList->setEnabled(idx, on);
                // If toggling off the currently active image, fall back to REF
                if (!on && idx == m_activeVol)
                    activateImage(0);
            });

    connect(m_imageList, &ImageListWidget::removeRequested,
            this, &MainWindow::removeImage);

    connect(m_imageList, &ImageListWidget::addRequested, this, [this] {
        // Try DICOM folder first, then a single image file
        QString p = QFileDialog::getOpenFileName(this, "Add Input Image", "",
            "NIfTI / MetaImage (*.nii *.nii.gz *.mhd *.mha *.nrrd);;All files (*)");
        if (p.isEmpty())
            p = QFileDialog::getExistingDirectory(this, "Add Input DICOM Folder");
        if (!p.isEmpty()) loadAdditionalImage(p);
    });

    // ── Viewer / ToolPanel connections ─────────────────────────────────────
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

    installRefChangeCallback();

    buildMenus();
    statusBar()->showMessage(
        "Ready — File → Open Image or Open DICOM  |  "
        "Ctrl+Scroll=zoom  Right-drag=W/L  Mid-drag=pan  "
        "Z=undo  [ ]=brush size  1-9=label");
}

// ── Helpers ───────────────────────────────────────────────────────────────────

ROIVolume* MainWindow::refVol() const
{
    return m_volumes.empty() ? nullptr : m_volumes[0].get();
}

ROIVolume* MainWindow::activeVol() const
{
    if (m_activeVol < 0 || m_activeVol >= (int)m_volumes.size())
        return refVol();
    return m_volumes[m_activeVol].get();
}

void MainWindow::installRefChangeCallback()
{
    if (refVol())
        refVol()->setChangeCallback([this]{
            m_viewer->refresh();
            m_toolPanel->refreshStats();
        });
}

// ── Menus ─────────────────────────────────────────────────────────────────────

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu("File");
    auto* openAct  = new QAction("Open Image…",  this);
    auto* dicomAct = new QAction("Open DICOM…",  this);
    openAct->setShortcut(QKeySequence::Open);
    fileMenu->addAction(openAct);
    fileMenu->addAction(dicomAct);
    fileMenu->addSeparator();

    m_recentMenu = fileMenu->addMenu("Open Recent");
    rebuildRecentMenu();
    fileMenu->addSeparator();

    auto* screenshotAct = new QAction("Save Screenshot…", this);
    screenshotAct->setShortcut(QKeySequence("Ctrl+Shift+S"));
    fileMenu->addSeparator();
    fileMenu->addAction(screenshotAct);
    fileMenu->addSeparator();

    auto* quitAct = new QAction("Quit", this);
    quitAct->setShortcut(QKeySequence::Quit);
    fileMenu->addAction(quitAct);

    connect(openAct,  &QAction::triggered, this, &MainWindow::openImage);
    connect(dicomAct, &QAction::triggered, this, &MainWindow::openDicom);
    connect(screenshotAct, &QAction::triggered, this, [this]{
        QString fn = QFileDialog::getSaveFileName(this, "Save Screenshot", "roisa_screenshot.png",
                                                  "PNG (*.png);;JPEG (*.jpg *.jpeg)");
        if (fn.isEmpty()) return;
        QPixmap px = m_viewer->grab();
        if (!px.save(fn))
            QMessageBox::critical(this, "Screenshot", "Failed to save:\n" + fn);
        else
            statusBar()->showMessage("Screenshot saved: " + fn);
    });
    connect(quitAct,  &QAction::triggered, this, &QWidget::close);

    auto* viewMenu  = menuBar()->addMenu("View");
    auto* resetWin  = new QAction("Reset W/L",        this);
    auto* resetZoom = new QAction("Reset All Zoom",    this);
    viewMenu->addAction(resetWin);
    viewMenu->addAction(resetZoom);

    viewMenu->addSeparator();
    auto* layoutMenu = viewMenu->addMenu("Layout");
    static const struct { const char* name; const char* key; } LAYOUT_ITEMS[5] = {
        {"2×2  (VTK | Sag / Cor | Axi)",        "1"},
        {"1+3  (Large Axial + three small)",     "2"},
        {"3-up  (Sag | Cor | Axi)",              "3"},
        {"Axial only",                            "4"},
        {"3D only",                               "5"},
    };
    for (int i = 0; i < 5; ++i) {
        auto* a = new QAction(LAYOUT_ITEMS[i].name, this);
        a->setShortcut(QKeySequence(QString("Ctrl+%1").arg(LAYOUT_ITEMS[i].key)));
        layoutMenu->addAction(a);
        connect(a, &QAction::triggered, this, [this, i]{ m_viewer->setLayoutPreset(i); });
    }

    viewMenu->addSeparator();
    auto* openBrowseAct = new QAction("Open in Series Browser…", this);
    openBrowseAct->setShortcut(QKeySequence("Ctrl+B"));
    viewMenu->addAction(openBrowseAct);
    connect(openBrowseAct, &QAction::triggered, this, [this]{
        QString dir = QFileDialog::getExistingDirectory(
                          this, "Open directory in Series Browser", QString());
        if (!dir.isEmpty()) m_browser->scanDirectory(dir);
    });
    connect(resetWin,  &QAction::triggered, this, [this]{
        if (activeVol() && activeVol()->isLoaded())
            { activeVol()->resetWindow(); m_viewer->refresh(); }});
    connect(resetZoom, &QAction::triggered, this, [this]{ m_viewer->resetAllZoom(); });

    auto* helpMenu = menuBar()->addMenu("Help");
    auto* aboutAct = new QAction("About", this);
    helpMenu->addAction(aboutAct);
    connect(aboutAct, &QAction::triggered, this, [this]{
        QMessageBox::about(this, "About ROISA",
            "<b>ROISA</b> — ROI Segmentation Assistant v1.0<br><br>"
            "<b>Multi-image:</b> File→Open loads the reference. "
            "Use the <b>Images</b> panel (＋ Add) to load additional input images. "
            "Click a row to view it; ● toggles it; ✕ removes it. "
            "ROI painting &amp; segmentation always operate on the reference.<br><br>"
            "<b>Keyboard shortcuts:</b><br>"
            "Z / Cmd+Z — Undo<br>"
            "1–9 — Select label<br>"
            "P — Paint mode &nbsp; E — Erase &nbsp; S — Segment<br>"
            "[ / ] — Decrease / Increase brush size<br>"
            "Ctrl+Scroll — Zoom in/out &nbsp; Right-drag — Adjust W/L<br>"
            "Middle-drag — Pan<br><br>"
            "Built with ITK 5.4 + Qt 6.7");
    });
}

// ── Keyboard ──────────────────────────────────────────────────────────────────

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Z:
        if (refVol() && refVol()->isLoaded()) {
            refVol()->undo();
            m_viewer->refresh();
            m_toolPanel->refreshStats();
        }
        break;
    case Qt::Key_1: case Qt::Key_2: case Qt::Key_3:
    case Qt::Key_4: case Qt::Key_5: case Qt::Key_6:
    case Qt::Key_7: case Qt::Key_8: case Qt::Key_9:
        statusBar()->showMessage(QString("Label %1").arg(e->key()-Qt::Key_0));
        break;
    case Qt::Key_BracketLeft:
        statusBar()->showMessage("Brush smaller  (use ToolPanel radius spinner)");
        break;
    case Qt::Key_BracketRight:
        statusBar()->showMessage("Brush larger  (use ToolPanel radius spinner)");
        break;
    case Qt::Key_P: statusBar()->showMessage("Paint mode");   break;
    case Qt::Key_E: statusBar()->showMessage("Erase mode");   break;
    case Qt::Key_S: statusBar()->showMessage("Segment mode"); break;
    case Qt::Key_Space: statusBar()->showMessage("Cycle tool"); break;
    default: QMainWindow::keyPressEvent(e);
    }
}

// ── Recent files ──────────────────────────────────────────────────────────────

void MainWindow::addRecentFile(const QString& path)
{
    QStringList recent = m_settings.value("recentFiles").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > MAX_RECENT) recent.removeLast();
    m_settings.setValue("recentFiles", recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    QStringList recent = m_settings.value("recentFiles").toStringList();
    for (const QString& p : recent) {
        auto* act = m_recentMenu->addAction(p);
        connect(act, &QAction::triggered, this, &MainWindow::openRecent);
    }
    if (recent.isEmpty())
        m_recentMenu->addAction("(none)")->setEnabled(false);
}

void MainWindow::openRecent()
{
    auto* act = qobject_cast<QAction*>(sender());
    if (act) loadPath(act->text());
}

// ── Load — reference (replaces everything) ────────────────────────────────────

void MainWindow::loadPath(const QString& path)
{
    statusBar()->showMessage("Loading " + path + " …");
    // Replace all volumes; REF is the freshly loaded one
    m_volumes.clear();
    m_volVisible.clear();
    m_volNames.clear();
    m_activeVol = 0;

    m_volumes.push_back(std::make_unique<ROIVolume>());
    if (!m_volumes[0]->load(path.toStdString())) {
        QMessageBox::critical(this, "Load error", "Failed to load:\n" + path);
        statusBar()->showMessage("Load failed.");
        // Leave one empty volume so the app stays valid
        m_volVisible.push_back(true);
        m_volNames.push_back("(none)");
        syncImageList();
        return;
    }
    m_volVisible.push_back(true);
    m_volNames.push_back(path);
    addRecentFile(path);
    afterLoad();
}

void MainWindow::openImage()
{
    QString p = QFileDialog::getOpenFileName(this, "Open Image", "",
        "NIfTI / MetaImage (*.nii *.nii.gz *.mhd *.mha *.nrrd);;All files (*)");
    if (!p.isEmpty()) loadPath(p);
}

void MainWindow::openDicom()
{
    QString d = QFileDialog::getExistingDirectory(this, "Select DICOM folder");
    if (!d.isEmpty()) loadPath(d);
}

void MainWindow::loadDicomSeries(const QString& dir, const QString& uid)
{
    statusBar()->showMessage("Loading DICOM series " + uid.left(20) + "…");
    m_volumes.clear();
    m_volVisible.clear();
    m_volNames.clear();
    m_activeVol = 0;

    m_volumes.push_back(std::make_unique<ROIVolume>());
    if (!m_volumes[0]->load(dir.toStdString(), uid.toStdString())) {
        QMessageBox::critical(this, "Load error",
                              "Failed to load series from:\n" + dir);
        statusBar()->showMessage("Load failed.");
        m_volVisible.push_back(true);
        m_volNames.push_back("(none)");
        syncImageList();
        return;
    }
    m_volVisible.push_back(true);
    m_volNames.push_back(dir);
    addRecentFile(dir);
    afterLoad();
}

void MainWindow::afterLoad()
{
    installRefChangeCallback();
    m_toolPanel->setVolume(refVol());
    m_toolPanel->setViewer(m_viewer);
    m_viewer->setVolume(refVol());
    m_viewer->refreshHistogram();
    syncImageList();
    statusBar()->showMessage(
        QString("Loaded REF: %1 × %2 × %3 voxels  |  spacing %.2f mm")
            .arg(refVol()->nx()).arg(refVol()->ny()).arg(refVol()->nz())
            .arg(refVol()->voxelSpacingMm()));
}

// ── Load — additional input image ─────────────────────────────────────────────

void MainWindow::loadAdditionalImage(const QString& path)
{
    statusBar()->showMessage("Adding input image: " + path + " …");
    auto vol = std::make_unique<ROIVolume>();
    if (!vol->load(path.toStdString())) {
        QMessageBox::critical(this, "Load error",
                              "Failed to load:\n" + path);
        statusBar()->showMessage("Add input image failed.");
        return;
    }
    m_volumes.push_back(std::move(vol));
    m_volVisible.push_back(true);
    m_volNames.push_back(path);
    addRecentFile(path);
    syncImageList();
    statusBar()->showMessage(
        QString("Added IN%1: %2").arg(m_volumes.size()-1)
                                 .arg(QFileInfo(path).fileName()));
}

// ── Image list actions ────────────────────────────────────────────────────────

void MainWindow::activateImage(int idx)
{
    if (idx < 0 || idx >= (int)m_volumes.size()) return;
    m_activeVol = idx;
    m_viewer->setVolume(m_volumes[idx].get());
    m_viewer->refresh();
    m_imageList->setActive(idx);
    statusBar()->showMessage(
        idx == 0
        ? "Viewing: REF — ROI operations active"
        : QString("Viewing: IN%1 — ROI operations always apply to REF").arg(idx));
}

void MainWindow::removeImage(int idx)
{
    if (idx < 0 || idx >= (int)m_volumes.size()) return;
    if (idx == 0 && m_volumes.size() == 1) return;  // can't remove only image
    if (idx == 0) {
        statusBar()->showMessage(
            "Cannot remove the reference image while input images exist. "
            "Open a new image to replace the reference.");
        return;
    }
    m_volumes.erase  (m_volumes.begin()    + idx);
    m_volVisible.erase(m_volVisible.begin() + idx);
    m_volNames.erase  (m_volNames.begin()   + idx);

    if (m_activeVol >= (int)m_volumes.size())
        m_activeVol = (int)m_volumes.size() - 1;
    else if (m_activeVol == idx)
        m_activeVol = 0;

    m_viewer->setVolume(m_volumes[m_activeVol].get());
    m_viewer->refresh();
    syncImageList();
}

void MainWindow::syncImageList()
{
    m_imageList->clear();
    for (int i = 0; i < (int)m_volumes.size(); ++i) {
        // Prefer the DICOM file basename; fall back to the stored path; fall back to label
        QString name = QFileInfo(
            QString::fromStdString(m_volumes[i]->firstDicomFile())).fileName();
        if (name.isEmpty())
            name = QFileInfo(m_volNames[i]).fileName();
        if (name.isEmpty())
            name = (i == 0) ? "REF" : QString("Input %1").arg(i);

        m_imageList->addImage(name, /*isRef=*/(i == 0));
        m_imageList->setEnabled(i, m_volVisible[i]);
        // Disable ✕ on REF when inputs exist
        if (i == 0)
            m_imageList->setRemoveEnabled(0, m_volumes.size() <= 1);
    }
    m_imageList->setActive(m_activeVol);
}

// ── Brush footprint ────────────────────────────────────────────────────────────

void MainWindow::brushFootprint(int cx, int cy, int cz,
                                 int radius, int shape,
                                 int viewAxis, bool twoD,
                                 std::vector<std::array<int,3>>& out) const
{
    if (!refVol()->isLoaded()) return;
    int NX=refVol()->nx(), NY=refVol()->ny(), NZ=refVol()->nz();
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

// ── Paint / erase — always on REF ─────────────────────────────────────────────

void MainWindow::onSeedOrPaint(int x, int y, int z)
{
    if (!refVol() || !refVol()->isLoaded()) return;
    QString mode = m_toolPanel->toolMode();
    if (mode != "paint" && mode != "erase") return;

    int16_t label = (mode=="paint")
                    ? static_cast<int16_t>(m_toolPanel->activeLabel())
                    : 0;

    if (!m_inStroke) {
        m_inStroke = true;
        m_strokeFirst.clear();
    }

    int  radius = m_toolPanel->brushRadius();
    int  shape  = m_toolPanel->brushShape();
    int  axis   = m_viewer->activeAxis();
    bool twoD   = m_toolPanel->twoDOnly();

    std::vector<std::array<int,3>> fp;
    brushFootprint(x, y, z, radius, shape, axis, twoD, fp);

    int16_t* buf = refVol()->maskImage()->GetBufferPointer();
    int NX = refVol()->nx(), NY = refVol()->ny();
    for (auto& [vx,vy,vz] : fp) {
        int lin = vx + NX*vy + NX*NY*vz;
        if (m_strokeFirst.find(lin)==m_strokeFirst.end())
            m_strokeFirst[lin] = buf[lin];
        buf[lin] = label;
    }
    refVol()->notifyChange();
}

void MainWindow::onMouseReleased()
{
    if (!m_inStroke || m_strokeFirst.empty()) { m_inStroke=false; return; }
    int NX=refVol()->nx(), NY=refVol()->ny();
    std::vector<std::array<int,3>> idxs;
    std::vector<int16_t>           olds;
    idxs.reserve(m_strokeFirst.size());
    olds.reserve(m_strokeFirst.size());
    for (auto& [lin,oldVal] : m_strokeFirst) {
        int z=lin/(NX*NY), y=(lin%(NX*NY))/NX, vx=lin%NX;
        idxs.push_back({vx,y,z}); olds.push_back(oldVal);
    }
    refVol()->pushUndo(std::move(idxs), std::move(olds));
    m_strokeFirst.clear();
    m_inStroke = false;
}
