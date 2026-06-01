// MainWindow.cpp — Top-level application window

#include "MainWindow.h"
#include "OrthoViewer.h"
#include "SeriesBrowser.h"
#include "ToolPanel.h"
#include "BgWorker.h"
#include "SettingsDialog.h"
#include "../core/ROIAlgorithms.h"
#include "../core/SUV.h"
#include "../core/Report.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPrinter>
#include <QProgressBar>
#include <QTextDocument>
#include <QThread>
#include <QUrl>
#include <fstream>
#include <memory>
#include <set>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBrush>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
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
    resize(1400, 860);
    setStyleSheet(
        "QMainWindow{background:#171717;}"
        "QWidget{background:#1d1d1f;color:#d2d2d4;font-size:12px;}"
        "QMenuBar{background:#111;color:#ccc;}"
        "QMenuBar::item:selected{background:#2a4a6a;}"
        "QMenu{background:#1e1e1e;color:#ccc;border:1px solid #333;}"
        "QMenu::item:selected{background:#2a4a6a;}"
        "QToolBar{background:#141414;border:none;spacing:3px;padding:3px;}"
        "QToolBar::separator{background:#333;width:1px;margin:4px 4px;}"
        "QToolButton{color:#cfcfd2;border-radius:4px;padding:4px 7px;}"
        "QToolButton:hover{background:#2a3a4a;}"
        "QToolButton:pressed{background:#1a3d5c;}"
        "QGroupBox{background:#222226;border:1px solid #34343a;border-radius:6px;"
        "  margin-top:10px;padding:8px 6px 6px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:9px;top:1px;padding:0 4px;"
        "  color:#7fb0d0;font-weight:bold;}"
        "QLabel{color:#c0c0c4;}"
        "QComboBox,QSpinBox,QDoubleSpinBox,QLineEdit{"
        "  background:#2a2a2e;color:#e2e2e4;border:1px solid #45454c;"
        "  border-radius:4px;padding:3px 5px;min-height:18px;}"
        "QComboBox:hover,QSpinBox:hover,QDoubleSpinBox:hover{border-color:#5a7fa0;}"
        "QPushButton{background:#34343a;color:#d8d8da;border:1px solid #4a4a52;"
        "  border-radius:5px;padding:5px 12px;}"
        "QPushButton:hover{background:#3e4a58;border-color:#5a7fa0;}"
        "QPushButton:pressed{background:#1a3d5c;}"
        "QPushButton:disabled{color:#666;background:#26262a;border-color:#333;}"
        "QPushButton[primary=\"true\"]{background:#22618f;color:#fff;"
        "  border:1px solid #2f7fbf;font-weight:bold;}"
        "QPushButton[primary=\"true\"]:hover{background:#2a72a8;}"
        "QTabWidget::pane{border:1px solid #34343a;border-radius:4px;top:-1px;}"
        "QTabBar::tab{background:#1d1d1f;color:#9a9aa0;padding:5px 12px;"
        "  border:1px solid #34343a;border-bottom:none;"
        "  border-top-left-radius:4px;border-top-right-radius:4px;}"
        "QTabBar::tab:selected{background:#2a3a4a;color:#cfe2f0;}"
        "QSlider::groove:horizontal{background:#3a3a40;height:5px;border-radius:3px;}"
        "QSlider::sub-page:horizontal{background:#3f7fb0;border-radius:3px;}"
        "QSlider::handle:horizontal{background:#cfe2f0;width:13px;height:13px;"
        "  margin:-5px 0;border-radius:7px;}"
        "QCheckBox{color:#c0c0c4;spacing:6px;}"
        "QTableWidget{background:#1e1e1e;color:#ddd;gridline-color:#333;"
        "  border:1px solid #34343a;border-radius:4px;}"
        "QHeaderView::section{background:#2a2a2e;color:#aaa;border:1px solid #34343a;padding:3px;}"
        "QStatusBar{background:#141414;color:#9aa;}"
        "QProgressBar{background:#2a2a2e;border:1px solid #444;border-radius:3px;}"
        "QProgressBar::chunk{background:#3f7fb0;}");

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
                rebuildFusion();   // recompose with the new visibility
            });

    // ── Fusion controls (Data Manager) target the selected layer ───────────
    connect(m_toolPanel, &ToolPanel::fusionColormapChanged, this, [this](int cm){
        if (m_activeVol == 0) m_viewer->setBaseColormap(cm);
        else if (m_activeVol < (int)m_volumes.size())
            m_volumes[m_activeVol]->setColormap(cm);
        rebuildFusion();
    });
    connect(m_toolPanel, &ToolPanel::fusionAlphaChanged, this, [this](float a){
        if (m_activeVol >= 0 && m_activeVol < (int)m_volumes.size())
            m_volumes[m_activeVol]->setFusionAlpha(a);
        rebuildFusion();
    });
    connect(m_toolPanel, &ToolPanel::fusionWindowChanged, this, [this](float lo, float hi){
        if (m_activeVol >= 0 && m_activeVol < (int)m_volumes.size())
            m_volumes[m_activeVol]->setWindow(lo, hi);
        rebuildFusion();
    });
    connect(m_toolPanel, &ToolPanel::baseVisibleToggled, this, [this](bool on){
        if (!m_volVisible.empty()) m_volVisible[0] = on;
        m_imageList->setEnabled(0, on);
        rebuildFusion();
    });

    // ── Registration operator (runs on a background thread) ──────────────────
    connect(m_toolPanel, &ToolPanel::registerRequested, this,
            [this](int movingIdx, const QString& mode, int iters){
        ROIVolume* ref = refVol();
        if (!ref || !ref->isLoaded()) { m_toolPanel->setRegStatus("Load a reference first."); return; }
        if (movingIdx < 1 || movingIdx >= (int)m_volumes.size()) {
            m_toolPanel->setRegStatus("Invalid moving image."); return; }
        const int m = (mode == "affine") ? 1 : (mode == "deformable") ? 2 : 0;
        ROIVolume* mv = m_volumes[movingIdx].get();
        // Snapshot immutable source images on the GUI thread; mutate on done.
        ROIVolume::FloatPtr moving = mv->ensureBackupAndMovingSource();
        ROIVolume::FloatPtr fixed  = ref->displayImage();
        auto result = std::make_shared<ROIVolume::FloatPtr>();
        auto txOut  = std::make_shared<ROIVolume::TransformPtr>();
        m_toolPanel->setRegStatus(QString("Registering IN%1 (%2)… working in background")
                                  .arg(movingIdx).arg(mode));
        runBg(
            [=]{ *result = ROIVolume::registerImages(moving, fixed, m, iters, txOut.get()); },
            [this, mv, result, txOut, movingIdx, mode]{
                if (*result) {
                    mv->applyRegisteredImage(*result);
                    mv->setLastTransform(*txOut);
                    m_toolPanel->setRegStatus(
                        QString("IN%1 registered to REF (%2). Now aligned in fusion.")
                            .arg(movingIdx).arg(mode));
                } else {
                    m_toolPanel->setRegStatus(
                        QString("Registration of IN%1 failed — see console.").arg(movingIdx));
                }
                rebuildFusion();
            },
            QString("Registering IN%1 to REF (%2)…").arg(movingIdx).arg(mode));
    });

    connect(m_toolPanel, &ToolPanel::manualTransformRequested, this,
            [this](int movingIdx, double tx, double ty, double tz,
                   double rx, double ry, double rz){
        ROIVolume* ref = refVol();
        if (!ref || !ref->isLoaded()) return;
        if (movingIdx < 1 || movingIdx >= (int)m_volumes.size()) return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = m_volumes[movingIdx]->applyManualTransform(ref, tx,ty,tz, rx,ry,rz);
        QApplication::restoreOverrideCursor();
        m_toolPanel->setRegStatus(ok
            ? QString("Manual transform applied to IN%1.").arg(movingIdx)
            : "Manual transform failed.");
        rebuildFusion();
    });

    connect(m_toolPanel, &ToolPanel::resetRegistrationRequested, this,
            [this](int movingIdx){
        if (movingIdx < 1 || movingIdx >= (int)m_volumes.size()) return;
        m_toolPanel->setRegStatus(m_volumes[movingIdx]->resetRegistration()
            ? QString("IN%1 restored to original (unregistered).").arg(movingIdx)
            : QString("IN%1 has no registration to reset.").arg(movingIdx));
        rebuildFusion();
    });

    connect(m_toolPanel, &ToolPanel::flipRequested, this,
            [this](int movingIdx, int axis){
        if (movingIdx < 1 || movingIdx >= (int)m_volumes.size()) return;
        static const char* kName[] = {"L/R", "A/P", "H/F"};
        const QString nm = (axis >= 0 && axis < 3) ? kName[axis] : "?";
        if (m_volumes[movingIdx]->flipAxis(axis)) {
            m_toolPanel->setRegStatus(QString("IN%1 flipped %2.").arg(movingIdx).arg(nm));
            rebuildFusion();
        } else {
            m_toolPanel->setRegStatus(QString("Could not flip IN%1.").arg(movingIdx));
        }
    });

    connect(m_toolPanel, &ToolPanel::saveTransformRequested, this, [this](int idx){
        if (idx < 1 || idx >= (int)m_volumes.size()) return;
        QString p = QFileDialog::getSaveFileName(this, "Save transform",
            QString("IN%1.tfm").arg(idx), "ITK transform (*.tfm)");
        if (p.isEmpty()) return;
        m_toolPanel->setRegStatus(m_volumes[idx]->saveTransform(p.toStdString())
            ? QString("Saved IN%1 transform → %2").arg(idx).arg(QFileInfo(p).fileName())
            : QString("IN%1 has no transform to save (register first).").arg(idx));
    });
    connect(m_toolPanel, &ToolPanel::loadTransformRequested, this, [this](int idx){
        if (idx < 1 || idx >= (int)m_volumes.size()) return;
        QString p = QFileDialog::getOpenFileName(this, "Load transform", "",
            "ITK transform (*.tfm *.txt *.mat);;All files (*)");
        if (p.isEmpty()) return;
        if (m_volumes[idx]->loadTransform(p.toStdString(), refVol())) {
            m_toolPanel->setRegStatus(QString("Applied transform to IN%1.").arg(idx));
            rebuildFusion();
        } else {
            m_toolPanel->setRegStatus(QString("Failed to load transform for IN%1.").arg(idx));
        }
    });

    // ── Quantification operator ──────────────────────────────────────────────
    connect(m_toolPanel, &ToolPanel::suvComputeRequested,  this, &MainWindow::onSuvCompute);
    connect(m_toolPanel, &ToolPanel::suvAutofillRequested, this, &MainWindow::onSuvAutofill);
    connect(m_toolPanel, &ToolPanel::suvExportRequested,   this, &MainWindow::onSuvExport);
    connect(m_toolPanel, &ToolPanel::tacComputeRequested,  this, &MainWindow::onTacCompute);
    connect(m_toolPanel, &ToolPanel::percentThresholdRequested, this, &MainWindow::onPercentThreshold);
    connect(m_toolPanel, &ToolPanel::roiRatioRequested,    this, &MainWindow::onRoiRatio);
    connect(m_toolPanel, &ToolPanel::roiHistRequested,     this, &MainWindow::onRoiHist);
    connect(m_toolPanel, &ToolPanel::interpolateRequested, this, &MainWindow::onInterpolate);
    connect(m_toolPanel, &ToolPanel::thresholdPreviewRequested, this, &MainWindow::onThresholdPreview);
    connect(m_toolPanel, &ToolPanel::kineticRequested,     this, &MainWindow::onKinetic);

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
    connect(m_toolPanel, &ToolPanel::toolModeChanged,
            this, &MainWindow::onPanelToolMode);
    connect(m_viewer, &OrthoViewer::polygonClosed, this, &MainWindow::onPolygon);

    installRefChangeCallback();

    buildMenus();
    buildToolbars();

    // Drag-and-drop loading
    setAcceptDrops(true);

    // Indeterminate progress indicator for background operations
    m_progress = new QProgressBar(this);
    m_progress->setMaximumWidth(140);
    m_progress->setRange(0, 0);          // indeterminate
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    statusBar()->addPermanentWidget(m_progress);
    connect(m_toolPanel, &ToolPanel::busyChanged, this,
            [this](bool busy){ if (m_progress) m_progress->setVisible(busy); });

    statusBar()->showMessage(
        "Ready — File → Open Image or Open DICOM  |  "
        "Ctrl+Scroll=zoom  Right-drag=W/L  Mid-drag=pan  "
        "Z=undo  [ ]=brush size  1-9=label  |  drag a file to load");
}

// ── Drag-and-drop ───────────────────────────────────────────────────────────────

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    const auto urls = e->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString p = urls.front().toLocalFile();
    if (!p.isEmpty()) {
        if (m_bgBusy || m_toolPanel->isSegRunning()) {
            statusBar()->showMessage("Busy — wait for the current operation to finish.");
            return;
        }
        loadPath(p);
    }
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
    auto* exportLabelsAct = new QAction("Export Labels (NIfTI)…", this);
    fileMenu->addAction(exportLabelsAct);
    auto* reportAct = new QAction("Generate Report (PDF/HTML)…", this);
    reportAct->setShortcut(QKeySequence("Ctrl+R"));
    fileMenu->addAction(reportAct);
    fileMenu->addSeparator();

    auto* saveSessAct = new QAction("Save Session…", this);
    saveSessAct->setShortcut(QKeySequence("Ctrl+S"));
    fileMenu->addAction(saveSessAct);
    auto* loadSessAct = new QAction("Load Session…", this);
    fileMenu->addAction(loadSessAct);
    fileMenu->addSeparator();

    auto* prefsAct = new QAction("Preferences…", this);
    prefsAct->setShortcut(QKeySequence("Ctrl+,"));
    fileMenu->addAction(prefsAct);
    fileMenu->addSeparator();

    auto* quitAct = new QAction("Quit", this);
    quitAct->setShortcut(QKeySequence::Quit);
    fileMenu->addAction(quitAct);

    connect(openAct,  &QAction::triggered, this, &MainWindow::openImage);
    connect(dicomAct, &QAction::triggered, this, &MainWindow::openDicom);
    connect(exportLabelsAct, &QAction::triggered, this, &MainWindow::onExportLabels);
    connect(reportAct, &QAction::triggered, this, &MainWindow::onGenerateReport);
    connect(saveSessAct, &QAction::triggered, this, &MainWindow::onSaveSession);
    connect(loadSessAct, &QAction::triggered, this, &MainWindow::onLoadSession);
    connect(prefsAct, &QAction::triggered, this, &MainWindow::onSettings);
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
    static const struct { const char* name; const char* key; } LAYOUT_ITEMS[6] = {
        {"2×2  (VTK | Sag / Cor | Axi)",        "1"},
        {"1+3  (Large Axial + three small)",     "2"},
        {"3-up  (Sag | Cor | Axi)",              "3"},
        {"Axial only",                            "4"},
        {"3D only",                               "5"},
        {"1×4  (Sag | Cor | Axi | 3D)",          "6"},
    };
    for (int i = 0; i < 6; ++i) {
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
    case Qt::Key_Z: doUndo(); break;
    case Qt::Key_1: case Qt::Key_2: case Qt::Key_3:
    case Qt::Key_4: case Qt::Key_5: case Qt::Key_6:
    case Qt::Key_7: case Qt::Key_8: case Qt::Key_9: {
        int lbl = e->key() - Qt::Key_0;
        m_toolPanel->setActiveLabelValue(lbl);
        statusBar()->showMessage(QString("Active label: %1").arg(lbl));
        updateStatusHeader();
        break; }
    case Qt::Key_BracketLeft:  m_toolPanel->bumpBrush(-1); break;
    case Qt::Key_BracketRight: m_toolPanel->bumpBrush(+1); break;
    case Qt::Key_V: selectTool("navigate"); break;
    case Qt::Key_P: selectTool("brush");    break;
    case Qt::Key_E: selectTool("eraser");   break;
    case Qt::Key_G: selectTool("polygon");  break;
    case Qt::Key_S: selectTool("segment");  break;
    case Qt::Key_M: selectTool("measure");  break;
    default: QMainWindow::keyPressEvent(e);
    }
}

// ── VQ-style chrome: top action bar, left tool rail, status header ──────────────

QIcon MainWindow::glyphIcon(const QString& glyph, const QString& color)
{
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c(color);
    p.setPen(QPen(c, 1.4));
    p.setBrush(QBrush(c.darker(260)));
    p.drawRoundedRect(2, 2, 24, 24, 6, 6);
    QFont f; f.setPixelSize(15); f.setBold(true);
    p.setFont(f); p.setPen(c);
    p.drawText(pm.rect(), Qt::AlignCenter, glyph);
    p.end();
    return QIcon(pm);
}

void MainWindow::doUndo()
{
    if (refVol() && refVol()->isLoaded()) {
        refVol()->undo();
        m_viewer->refresh();
        m_toolPanel->refreshStats();
        statusBar()->showMessage("Undo");
    }
}

void MainWindow::buildToolbars()
{
    QStyle* st = style();

    // ── Top action toolbar (global file/workflow verbs) ───────────────────────
    auto* top = new QToolBar("Actions", this);
    top->setMovable(false);
    top->setIconSize(QSize(18, 18));
    top->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addToolBar(Qt::TopToolBarArea, top);

    auto addAct = [&](QStyle::StandardPixmap sp, const QString& text,
                      auto slot, const QString& sc = QString()) -> QAction* {
        auto* a = new QAction(st->standardIcon(sp), text, this);
        connect(a, &QAction::triggered, this, slot);
        if (!sc.isEmpty()) a->setShortcut(QKeySequence(sc));
        a->setToolTip(text);
        top->addAction(a);
        return a;
    };

    addAct(QStyle::SP_DialogOpenButton, "Open", &MainWindow::openImage, "Ctrl+O");
    addAct(QStyle::SP_DirOpenIcon,      "DICOM", &MainWindow::openDicom);
    top->addSeparator();
    addAct(QStyle::SP_DialogSaveButton, "Save Session", &MainWindow::onSaveSession, "Ctrl+S");
    addAct(QStyle::SP_DialogOpenButton, "Load Session", &MainWindow::onLoadSession);
    addAct(QStyle::SP_FileIcon,         "Export Labels", &MainWindow::onExportLabels);
    top->addSeparator();
    addAct(QStyle::SP_ArrowBack,        "Undo", &MainWindow::doUndo, "Ctrl+Z");
    addAct(QStyle::SP_FileDialogContentsView, "Report", &MainWindow::onGenerateReport, "Ctrl+R");
    {
        auto* shot = new QAction(st->standardIcon(QStyle::SP_DialogApplyButton),
                                 "Snapshot", this);
        shot->setShortcut(QKeySequence("Ctrl+Shift+S"));
        connect(shot, &QAction::triggered, this, [this]{
            QString fn = QFileDialog::getSaveFileName(this, "Save Screenshot",
                          "roisa_screenshot.png", "PNG (*.png);;JPEG (*.jpg *.jpeg)");
            if (fn.isEmpty()) return;
            if (!m_viewer->grab().save(fn))
                QMessageBox::critical(this, "Screenshot", "Failed to save:\n" + fn);
            else statusBar()->showMessage("Screenshot saved: " + fn);
        });
        top->addAction(shot);
    }
    top->addSeparator();
    addAct(QStyle::SP_FileDialogDetailedView, "Preferences", &MainWindow::onSettings, "Ctrl+,");

    // spacer pushes the status header to the right edge
    auto* spacer = new QWidget(top);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    top->addWidget(spacer);
    m_statusHeader = new QLabel(top);
    m_statusHeader->setTextFormat(Qt::RichText);
    m_statusHeader->setStyleSheet("color:#cfe2f0;font-size:12px;padding:2px 10px;");
    top->addWidget(m_statusHeader);

    // ── Left tool rail (interaction tools, always visible) ────────────────────
    auto* rail = new QToolBar("Tools", this);
    rail->setMovable(false);
    rail->setIconSize(QSize(28, 28));
    rail->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    rail->setStyleSheet(
        "QToolBar{background:#141414;border-right:1px solid #2a2a2a;spacing:2px;}"
        "QToolButton{color:#9aa;font-size:9px;padding:4px 2px;border-radius:4px;}"
        "QToolButton:hover{background:#222;}"
        "QToolButton:checked{background:#1c3550;color:#cfe2f0;}");
    addToolBar(Qt::LeftToolBarArea, rail);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);

    struct ToolDef { const char* key; const char* label; const char* glyph;
                     const char* color; };
    const ToolDef tools[] = {
        {"navigate", "Nav",    "✺", "#7fb0d0"},   // ✥-like
        {"brush",    "Brush",  "✎", "#e6c84b"},   // ✎
        {"eraser",   "Eraser", "⌫", "#e67a4b"},   // ⌫
        {"polygon",  "Poly",   "▱", "#4bd0c8"},   // ▱
        {"segment",  "Seg",    "◐", "#9b6cf0"},   // ◐
        {"measure",  "Ruler",  "⊾", "#5bd06a"},   // ⊾
    };
    for (const auto& t : tools) {
        QString key(t.key);
        auto* a = new QAction(glyphIcon(QString::fromUtf8(t.glyph), t.color),
                              t.label, this);
        a->setCheckable(true);
        a->setToolTip(t.label);
        connect(a, &QAction::triggered, this, [this, key]{ selectTool(key); });
        m_toolGroup->addAction(a);
        rail->addAction(a);
        m_toolActs.insert(key, a);
    }
    if (auto* nav = m_toolActs.value("navigate")) nav->setChecked(true);

    updateStatusHeader();
}

void MainWindow::selectTool(const QString& key)
{
    m_activeTool = key;
    m_toolSync = true;
    if (key == "navigate")      m_toolPanel->setOperator(0);
    else if (key == "measure")  m_toolPanel->setOperator(3);
    else if (key == "brush")    m_toolPanel->setToolByName("paint");
    else if (key == "eraser")   m_toolPanel->setToolByName("erase");
    else if (key == "segment")  m_toolPanel->setToolByName("segment");
    else if (key == "polygon")  m_toolPanel->setToolByName("polygon");
    m_toolSync = false;

    if (auto* a = m_toolActs.value(key)) { if (!a->isChecked()) a->setChecked(true); }
    QString cap = key.left(1).toUpper() + key.mid(1);
    statusBar()->showMessage("Tool: " + cap);
    updateStatusHeader();
}

void MainWindow::onPanelToolMode(const QString& mode)
{
    // Reflect any tool/module change (keyboard, ROI tab, operator dropdown) on
    // the rail so the active-tool state never lies. "" → Navigation /
    // Registration / Quantification (no drawing tool).
    m_viewer->setPolygonMode(mode == "polygon");
    if (m_toolSync) return;
    QString key;
    if      (mode == "paint")   key = "brush";
    else if (mode == "erase")   key = "eraser";
    else if (mode == "segment") key = "segment";
    else if (mode == "polygon") key = "polygon";
    else if (mode == "measure") key = "measure";
    else                        key = "navigate";
    m_activeTool = key;
    if (auto* a = m_toolActs.value(key)) { if (!a->isChecked()) a->setChecked(true); }
    updateStatusHeader();
}

void MainWindow::updateStatusHeader()
{
    if (!m_statusHeader) return;
    static const char* kLabelColors[] = {
        "#000000", "#e6194b", "#3cb44b", "#4363d8", "#f58231", "#911eb4",
        "#42d4f4", "#f032e6", "#bfef45", "#fabed4", "#469990" };
    QString tool = m_activeTool.left(1).toUpper() + m_activeTool.mid(1);
    int lab = m_toolPanel ? m_toolPanel->activeLabel() : 1;
    QString color = (lab > 0 && lab < 11) ? kLabelColors[lab] : "#ccc";
    QString ref = m_volNames.empty() ? "(none)" : m_volNames[0];
    if (ref == "(none)" || !refVol() || !refVol()->isLoaded())
        ref = "no reference loaded";
    else
        ref = QFileInfo(ref).fileName();
    m_statusHeader->setText(
        QString("<b>%1</b>&nbsp;&nbsp;·&nbsp;&nbsp;Label "
                "<span style='color:%2;font-size:15px'>■</span> %3"
                "&nbsp;&nbsp;·&nbsp;&nbsp;<span style='color:#9ab'>REF:</span> %4")
            .arg(tool, color).arg(lab).arg(ref));
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
    if (m_bgBusy || m_toolPanel->isSegRunning()) {
        statusBar()->showMessage("Busy — wait for the current operation to finish.");
        return;
    }
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
    m_viewer->setBaseVisible(true);
    m_viewer->setOverlays({});       // fresh REF — no overlays yet
    m_viewer->refreshHistogram();
    syncImageList();
    pushFusionTarget();
    statusBar()->showMessage(
        QString("Loaded REF: %1 × %2 × %3 voxels  |  spacing %.2f mm")
            .arg(refVol()->nx()).arg(refVol()->ny()).arg(refVol()->nz())
            .arg(refVol()->voxelSpacingMm()));
    updateStatusHeader();
}

// ── Fusion ──────────────────────────────────────────────────────────────────────

void MainWindow::rebuildFusion()
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) return;

    // Base layer is always REF (owns geometry + mask)
    m_viewer->setVolume(ref);
    m_viewer->setBaseVisible(m_volVisible.empty() ? true : m_volVisible[0]);

    std::vector<SliceView::FusionLayer> overlays;
    for (int i = 1; i < (int)m_volumes.size(); ++i) {
        if (i >= (int)m_volVisible.size() || !m_volVisible[i]) continue;
        ROIVolume* v = m_volumes[i].get();
        auto arr = v->resampleDisplayTo(ref);
        if (!arr) continue;
        SliceView::FusionLayer layer;
        layer.arr      = arr;
        layer.colormap = v->colormap();
        layer.alpha    = v->fusionAlpha();
        layer.wmin     = v->vmin();
        layer.wmax     = v->vmax();
        overlays.push_back(std::move(layer));
    }
    m_viewer->setOverlays(overlays);
    m_viewer->refresh();
}

void MainWindow::pushFusionTarget()
{
    int idx = m_activeVol;
    if (idx < 0 || idx >= (int)m_volumes.size()) return;
    ROIVolume* v = m_volumes[idx].get();
    bool isBase  = (idx == 0);
    QString name = isBase ? "REF" : QString("IN%1").arg(idx);
    int cm = isBase ? m_viewer->baseColormap() : v->colormap();
    m_toolPanel->setFusionTarget(
        name, cm, v->fusionAlpha(), v->vmin(), v->vmax(), isBase,
        m_volVisible.empty() ? true : m_volVisible[0]);
}

// ── Load — additional input image ─────────────────────────────────────────────

void MainWindow::loadAdditionalImage(const QString& path)
{
    if (m_bgBusy || m_toolPanel->isSegRunning()) {
        statusBar()->showMessage("Busy — wait for the current operation to finish.");
        return;
    }
    statusBar()->showMessage("Adding input image: " + path + " …");
    auto vol = std::make_unique<ROIVolume>();
    if (!vol->load(path.toStdString())) {
        QMessageBox::critical(this, "Load error",
                              "Failed to load:\n" + path);
        statusBar()->showMessage("Add input image failed.");
        return;
    }
    // New overlays default to a 'hot' colormap so they stand out on REF
    vol->setColormap(SliceView::HOT);
    m_volumes.push_back(std::move(vol));
    m_volVisible.push_back(true);
    m_volNames.push_back(path);
    addRecentFile(path);
    syncImageList();
    rebuildFusion();
    statusBar()->showMessage(
        QString("Added IN%1: %2 — fused over REF").arg(m_volumes.size()-1)
                                 .arg(QFileInfo(path).fileName()));
}

// ── Image list actions ────────────────────────────────────────────────────────

void MainWindow::activateImage(int idx)
{
    // Select a layer for editing — does NOT change what is composited
    // (REF base + all visible inputs always render together).
    if (idx < 0 || idx >= (int)m_volumes.size()) return;
    m_activeVol = idx;
    m_imageList->setActive(idx);
    pushFusionTarget();
    statusBar()->showMessage(
        idx == 0
        ? "Selected REF (base) — ROI operations apply here"
        : QString("Selected IN%1 — adjust its colormap / opacity / window").arg(idx));
}

void MainWindow::removeImage(int idx)
{
    if (m_bgBusy || m_toolPanel->isSegRunning()) {
        statusBar()->showMessage("Busy — wait for the current operation to finish.");
        return;
    }
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

    syncImageList();
    rebuildFusion();
    pushFusionTarget();
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

    // Keep the Registration operator's moving-image dropdown in sync
    QList<QPair<QString,int>> moving;
    for (int i = 1; i < (int)m_volumes.size(); ++i) {
        QString nm = QFileInfo(m_volNames[i]).fileName();
        moving.append({ QString("IN%1%2").arg(i)
                            .arg(nm.isEmpty() ? "" : "  (" + nm + ")"), i });
    }
    m_toolPanel->setMovingImages(moving);

    // Keep the Quantification activity-image dropdown in sync (REF + inputs)
    QList<QPair<QString,int>> quant;
    quant.append({ "REF", 0 });
    for (int i = 1; i < (int)m_volumes.size(); ++i)
        quant.append({ QString("IN%1").arg(i), i });
    m_toolPanel->setQuantImages(quant);
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
    // Gate on toolMode() — it is operator-aware (returns "" for Navigation /
    // Registration / Quantification, "measure" for Measure), so painting is
    // disabled outside the ROI module even if a paint tool was last active.
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

    // Smart (edge-aware) brush: only paint voxels within tolerance of the
    // brush-centre intensity. Erasing is never gated.
    const bool   smart = (mode == "paint") && m_toolPanel->smartBrush();
    const double tol   = m_toolPanel->brushTolerance();
    const float* img   = smart ? refVol()->displayImage()->GetBufferPointer() : nullptr;
    const float  ref   = smart ? refVol()->getIntensity(x, y, z) : 0.f;

    for (auto& [vx,vy,vz] : fp) {
        int lin = vx + NX*vy + NX*NY*vz;
        if (smart && std::abs(img[lin] - ref) > tol) continue;
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

// ── Background-task runner ──────────────────────────────────────────────────────

void MainWindow::runBg(std::function<void()> work, std::function<void()> onDone,
                       const QString& busyMsg)
{
    if (m_bgBusy || m_toolPanel->isSegRunning()) {
        statusBar()->showMessage("Busy — wait for the current operation to finish.");
        return;
    }
    m_bgBusy = true;
    m_toolPanel->setBusy(true);
    statusBar()->showMessage(busyMsg);

    auto* thread = new QThread(this);
    auto* worker = new BgWorker(std::move(work));
    worker->moveToThread(thread);
    auto onDonePtr = std::make_shared<std::function<void()>>(std::move(onDone));

    connect(thread, &QThread::started, worker, &BgWorker::run);
    connect(worker, &BgWorker::done, this, [this, thread, onDonePtr]{
        m_bgBusy = false; m_toolPanel->setBusy(false);
        (*onDonePtr)();
        thread->quit();
    });
    connect(worker, &BgWorker::failed, this, [this, thread](const QString& msg){
        m_bgBusy = false; m_toolPanel->setBusy(false);
        statusBar()->showMessage("Operation failed: " + msg);
        thread->quit();
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// ── Quantification ──────────────────────────────────────────────────────────────

void MainWindow::onSuvCompute(int activityIdx)
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) { statusBar()->showMessage("Load a reference first."); return; }

    // Snapshot read-only inputs on the GUI thread.
    ROIVolume* actVol = (activityIdx <= 0 || activityIdx >= (int)m_volumes.size())
                        ? ref : m_volumes[activityIdx].get();
    const int16_t* msk = ref->maskImage()->GetBufferPointer();
    const int nx = ref->nx(), ny = ref->ny(), nz = ref->nz();
    const double sp = ref->voxelSpacingMm();
    const double f  = SUV::factor(m_toolPanel->suvParams());
    auto rows = std::make_shared<std::vector<ROISUVStats>>();

    runBg(
        [=]{
            ROIVolume::FloatPtr actImg = (actVol == ref)
                ? actVol->displayImage()
                : actVol->resampleDisplayTo(ref);        // heavy resample off-thread
            if (!actImg) return;
            const float* act = actImg->GetBufferPointer();
            std::set<int> labels;
            const long n = static_cast<long>(nx) * ny * nz;
            for (long i = 0; i < n; ++i) if (msk[i] > 0) labels.insert(msk[i]);
            for (int lbl : labels) {
                ROISUVStats st;
                if (SUV::roiStats(act, msk, nx, ny, nz, lbl, sp, sp, sp, f, st))
                    rows->push_back(st);
            }
        },
        [this, rows, activityIdx, f]{
            m_toolPanel->setQuantResults(*rows);
            statusBar()->showMessage(
                QString("SUV computed for %1 ROI(s) on %2  (factor %3)")
                    .arg(rows->size())
                    .arg(activityIdx == 0 ? "REF" : QString("IN%1").arg(activityIdx))
                    .arg(f, 0, 'e', 4));
        },
        "Computing SUV…");
}

void MainWindow::onSuvAutofill(int activityIdx)
{
    if (activityIdx < 0 || activityIdx >= (int)m_volumes.size()) return;
    std::string path = m_volumes[activityIdx]->firstDicomFile();
    if (path.empty()) path = m_volNames[activityIdx].toStdString();
    bool ok = false;
    SUVParams p = SUV::extractParams(path, ok);
    if (!ok) {
        statusBar()->showMessage(
            "Auto-fill failed — no DICOM metadata found. Enter values manually.");
        return;
    }
    m_toolPanel->setSuvParams(p);
    statusBar()->showMessage("SUV parameters auto-filled from DICOM.");
}

void MainWindow::onSuvExport()
{
    const auto& rows = m_toolPanel->quantResults();
    if (rows.empty()) { statusBar()->showMessage("Nothing to export — Compute SUV first."); return; }
    QString fn = QFileDialog::getSaveFileName(this, "Export SUV CSV",
                                              "roisa_suv.csv", "CSV (*.csv)");
    if (fn.isEmpty()) return;
    std::ofstream f(fn.toStdString());
    if (!f) { QMessageBox::warning(this, "Export error", "Cannot write file."); return; }
    f << "Label,Voxels,Volume_mL,SUVmean,SUVmax,SUVpeak,TLG\n";
    for (const auto& d : rows)
        f << d.label << "," << d.voxels << "," << d.volumeMl << ","
          << d.suvMean << "," << d.suvMax << "," << d.suvPeak << "," << d.tlg << "\n";
    statusBar()->showMessage("Exported SUV table → " + QFileInfo(fn).fileName());
}

void MainWindow::onTacCompute(int label, int activityIdx)
{
    Q_UNUSED(activityIdx);
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) return;

    const int16_t* msk = ref->maskImage()->GetBufferPointer();
    const int nx = ref->nx(), ny = ref->ny(), nz = ref->nz();
    const double f = SUV::factor(m_toolPanel->suvParams());
    const int nInputs = (int)m_volumes.size();
    auto tacResult = std::make_shared<std::vector<double>>();

    runBg(
        [=]{
            std::vector<ROIVolume::FloatPtr> hold;        // keep images alive
            std::vector<const float*>        frames;
            for (int i = 1; i < nInputs; ++i) {
                auto img = m_volumes[i]->resampleDisplayTo(ref);  // heavy off-thread
                if (img) { hold.push_back(img); frames.push_back(img->GetBufferPointer()); }
            }
            if (frames.empty()) {
                hold.push_back(ref->displayImage());
                frames.push_back(ref->displayImage()->GetBufferPointer());
            }
            *tacResult = SUV::tac(frames, msk, nx, ny, nz, label, f);
        },
        [this, tacResult, label]{
            if (tacResult->empty()) {
                statusBar()->showMessage(QString("Label %1 not present in the ROI mask.").arg(label));
                m_toolPanel->setTac({});
                return;
            }
            m_toolPanel->setTac(*tacResult, "SUVmean");
            statusBar()->showMessage(
                QString("TAC plotted for label %1 across %2 frame(s).")
                    .arg(label).arg(tacResult->size()));
        },
        "Computing time-activity curve…");
}

// ── Analysis ────────────────────────────────────────────────────────────────────

// Returns the activity image at `idx` resampled onto the REF grid (or REF itself).
static ROIVolume::FloatPtr activityOnRefGrid(
        const std::vector<std::unique_ptr<ROIVolume>>& vols, int idx, ROIVolume* ref)
{
    if (idx <= 0 || idx >= (int)vols.size()) return ref->displayImage();
    return vols[idx]->resampleDisplayTo(ref);
}

void MainWindow::onPercentThreshold(int sourceLabel, double pct, int targetLabel)
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) { statusBar()->showMessage("Load a reference first."); return; }
    ROIVolume::FloatPtr actImg = activityOnRefGrid(m_volumes, m_toolPanel->activityIndex(), ref);
    if (!actImg) { statusBar()->showMessage("No activity image available."); return; }

    ref->pushUndoAll();
    const long count = SUV::percentThreshold(
        actImg->GetBufferPointer(), ref->maskImage()->GetBufferPointer(),
        ref->nx(), ref->ny(), ref->nz(), sourceLabel, pct, targetLabel);
    if (count < 0) {
        statusBar()->showMessage("Percent threshold: source region empty or non-positive peak.");
        return;
    }
    ref->notifyChange();
    rebuildFusion();
    m_toolPanel->refreshStats();
    statusBar()->showMessage(
        QString("Percent threshold: %1 voxels ≥ %2% of peak → label %3.")
            .arg(count).arg(pct, 0, 'f', 0).arg(targetLabel));
}

void MainWindow::onRoiRatio(int labelA, int labelB)
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) return;
    ROIVolume::FloatPtr actImg = activityOnRefGrid(m_volumes, m_toolPanel->activityIndex(), ref);
    if (!actImg) return;
    double mA, mB, ratio;
    if (!SUV::roiRatio(actImg->GetBufferPointer(), ref->maskImage()->GetBufferPointer(),
                       ref->nx(), ref->ny(), ref->nz(), labelA, labelB, mA, mB, ratio)) {
        m_toolPanel->setRoiRatioResult(QString("Label %1 or %2 is empty.").arg(labelA).arg(labelB));
        return;
    }
    m_toolPanel->setRoiRatioResult(
        QString("mean(L%1)=%2   mean(L%3)=%4\nratio A/B = %5")
            .arg(labelA).arg(mA, 0, 'g', 4).arg(labelB).arg(mB, 0, 'g', 4)
            .arg(ratio, 0, 'f', 3));
    statusBar()->showMessage(QString("ROI ratio L%1/L%2 = %3").arg(labelA).arg(labelB).arg(ratio, 0, 'f', 3));
}

void MainWindow::onRoiHist(int label)
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) return;
    ROIVolume::FloatPtr actImg = activityOnRefGrid(m_volumes, m_toolPanel->activityIndex(), ref);
    if (!actImg) return;
    std::vector<double> counts; double vmin, vmax;
    if (!SUV::roiHistogram(actImg->GetBufferPointer(), ref->maskImage()->GetBufferPointer(),
                           ref->nx(), ref->ny(), ref->nz(), label, 64, counts, vmin, vmax)) {
        m_toolPanel->setRoiHist({}, 0., 1., QString("Label %1: empty").arg(label));
        statusBar()->showMessage(QString("Label %1 is empty.").arg(label));
        return;
    }
    m_toolPanel->setRoiHist(counts, vmin, vmax, QString("Label %1 histogram").arg(label));
    statusBar()->showMessage(QString("Histogram of label %1.").arg(label));
}

void MainWindow::onKinetic(int target, int input, const QString& model,
                           double dt, int fitFrom)
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) return;
    const int16_t* msk = ref->maskImage()->GetBufferPointer();
    const int nx = ref->nx(), ny = ref->ny(), nz = ref->nz();

    std::vector<ROIVolume::FloatPtr> hold;
    std::vector<const float*> frames;
    for (int i = 1; i < (int)m_volumes.size(); ++i) {
        auto img = m_volumes[i]->resampleDisplayTo(ref);
        if (img) { hold.push_back(img); frames.push_back(img->GetBufferPointer()); }
    }
    if (frames.size() < 3) {
        m_toolPanel->setKineticResult(
            "Need ≥3 input frames (load the dynamic series as inputs).", {});
        return;
    }
    auto tissue = SUV::tac(frames, msk, nx, ny, nz, target, 1.0);
    auto blood  = SUV::tac(frames, msk, nx, ny, nz, input,  1.0);
    if (tissue.empty() || blood.empty()) {
        m_toolPanel->setKineticResult(
            QString("Label %1 or %2 not present in the mask.").arg(target).arg(input), {});
        return;
    }
    SUV::KineticResult r = (model == "patlak")
        ? SUV::patlak(tissue, blood, dt, fitFrom)
        : SUV::logan (tissue, blood, dt, fitFrom);
    if (!r.ok) {
        m_toolPanel->setKineticResult("Fit failed (too few valid points).", {});
        return;
    }
    m_toolPanel->setKineticResult(
        QString("%1:  %2 = %3   (intercept %4, fit from frame %5)")
            .arg(QString::fromStdString(r.model), QString::fromStdString(r.param))
            .arg(r.slope, 0, 'g', 4).arg(r.intercept, 0, 'g', 3).arg(fitFrom),
        r.y);
    statusBar()->showMessage(
        QString("%1 %2 = %3").arg(QString::fromStdString(r.model),
                                  QString::fromStdString(r.param)).arg(r.slope, 0, 'g', 4));
}

// ── Segmentation/ROI tools ──────────────────────────────────────────────────────

void MainWindow::onInterpolate(int label, int axis)
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) return;
    ref->pushUndoAll();
    const int filled = ref->interpolateLabel(label, axis);
    rebuildFusion();
    m_toolPanel->refreshStats();
    statusBar()->showMessage(filled
        ? QString("Interpolated label %1: filled %2 slice(s).").arg(label).arg(filled)
        : QString("Interpolate: need label %1 on ≥2 separated slices along that axis.").arg(label));
}

void MainWindow::onThresholdPreview(double lo, double hi, bool on)
{
    ROIVolume* ref = refVol();
    if (!on || !ref || !ref->isLoaded()) {
        m_previewVol.clear();
        m_viewer->setPreviewBuffer(nullptr);
        m_viewer->refresh();
        return;
    }
    const float* img = ref->displayImage()->GetBufferPointer();
    const long n = static_cast<long>(ref->nx()) * ref->ny() * ref->nz();
    m_previewVol.resize(n);
    for (long i = 0; i < n; ++i)
        m_previewVol[i] = (img[i] >= lo && img[i] <= hi) ? 1 : 0;
    m_viewer->setPreviewBuffer(m_previewVol.data());
    m_viewer->refresh();
}

// ── I/O & workflow ──────────────────────────────────────────────────────────────

void MainWindow::onExportLabels()
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) { statusBar()->showMessage("No mask to export."); return; }
    auto labels = ref->presentLabels();
    if (labels.empty()) { statusBar()->showMessage("Mask is empty — nothing to export."); return; }
    QString dir = QFileDialog::getExistingDirectory(this, "Export labels to folder");
    if (dir.isEmpty()) return;
    int written = 0;
    for (int lbl : labels) {
        QString path = QDir(dir).filePath(QString("label_%1.nii.gz").arg(lbl));
        if (ref->saveLabelBinary(lbl, path.toStdString())) ++written;
    }
    statusBar()->showMessage(QString("Exported %1 label mask(s) → %2").arg(written).arg(dir));
}

void MainWindow::onSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
        m_toolPanel->applyPreferences(dlg.brushRadius(), dlg.colormap(), dlg.halfLifeS());
}

void MainWindow::onGenerateReport()
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) { statusBar()->showMessage("Load an image first."); return; }
    QString path = QFileDialog::getSaveFileName(this, "Save report",
        "roisa_report.pdf", "PDF (*.pdf);;HTML (*.html)");
    if (path.isEmpty()) return;

    std::vector<std::pair<QString,QString>> info = {
        {"Name", QFileInfo(m_volNames[0]).fileName()},
        {"Dimensions", QString("%1 × %2 × %3 voxels").arg(ref->nx()).arg(ref->ny()).arg(ref->nz())},
        {"Spacing", QString("%1 mm (isotropic)").arg(ref->voxelSpacingMm(), 0, 'f', 3)},
        {"Window", QString("%1 … %2").arg(ref->vmin(), 0, 'f', 1).arg(ref->vmax(), 0, 'f', 1)},
    };

    const std::pair<QString,QPixmap> shots[] = {
        {"Sagittal", m_viewer->grabSagittal()},
        {"Coronal",  m_viewer->grabCoronal()},
        {"Axial",    m_viewer->grabAxial()},
        {"3-D",      m_viewer->grabVtk()},
    };

    std::vector<QStringList> stats;
    for (const auto& s : ref->computeAllStats())
        stats.push_back({ QString::number(s.label), QString::number(s.voxelCount),
                          QString::number(s.volumeMm3, 'f', 1),
                          QString::number(s.meanIntensity, 'f', 2),
                          QString::number(s.stdIntensity, 'f', 2) });

    std::vector<QStringList> suv;
    for (const auto& d : m_toolPanel->quantResults())
        suv.push_back({ QString::number(d.label), QString::number(d.volumeMl, 'f', 3),
                        QString::number(d.suvMean, 'f', 2), QString::number(d.suvMax, 'f', 2),
                        QString::number(d.suvPeak, 'f', 2), QString::number(d.tlg, 'f', 2) });

    const QStringList meas = m_viewer->measurements();

    if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
        QTextDocument doc;
        std::vector<std::pair<QString,QString>> imgs;
        for (int i = 0; i < 4; ++i) {
            const QString name = QString("shot%1").arg(i);
            doc.addResource(QTextDocument::ImageResource, QUrl(name),
                            shots[i].second.toImage());
            imgs.push_back({shots[i].first, name});
        }
        doc.setHtml(Report::buildHtml("ROISA Report", info, imgs, stats, suv, meas));
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        doc.print(&printer);
    } else {
        std::vector<std::pair<QString,QString>> imgs;
        for (int i = 0; i < 4; ++i) {
            QByteArray ba; QBuffer buf(&ba); buf.open(QIODevice::WriteOnly);
            shots[i].second.save(&buf, "PNG");
            imgs.push_back({shots[i].first,
                            "data:image/png;base64," + QString::fromLatin1(ba.toBase64())});
        }
        QString html = Report::buildHtml("ROISA Report", info, imgs, stats, suv, meas);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) { f.write(html.toUtf8()); f.close(); }
    }
    statusBar()->showMessage("Report saved: " + QFileInfo(path).fileName());
}

// ── Session save / load ─────────────────────────────────────────────────────────

void MainWindow::onSaveSession()
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded()) { statusBar()->showMessage("Nothing to save."); return; }
    QString dir = QFileDialog::getExistingDirectory(this, "Save session to folder");
    if (dir.isEmpty()) return;

    QJsonObject manifest;
    manifest["version"] = 1;
    manifest["active"]  = m_activeVol;
    manifest["preset"]  = m_viewer->layoutPreset();

    SUVParams p = m_toolPanel->suvParams();
    QJsonObject suv;
    suv["suvType"]=p.suvType; suv["weightKg"]=p.weightKg; suv["heightCm"]=p.heightCm;
    suv["sex"]=p.sex; suv["doseMbq"]=p.doseMbq; suv["halfLifeS"]=p.halfLifeS;
    suv["decayMin"]=p.decayMin;
    manifest["suv"] = suv;

    QJsonArray vols;
    for (int i = 0; i < (int)m_volumes.size(); ++i) {
        ROIVolume* v = m_volumes[i].get();
        QJsonObject o;
        o["path"]=m_volNames[i]; o["colormap"]=v->colormap(); o["alpha"]=v->fusionAlpha();
        o["visible"]=(bool)m_volVisible[i]; o["wmin"]=v->vmin(); o["wmax"]=v->vmax();
        vols.append(o);
    }
    manifest["volumes"] = vols;

    QFile f(QDir(dir).filePath("session.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(manifest).toJson()); f.close();
    }
    ref->saveMaskRaw(QDir(dir).filePath("mask.nii.gz").toStdString());
    statusBar()->showMessage("Session saved → " + dir);
}

void MainWindow::onLoadSession()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Load session folder");
    if (dir.isEmpty()) return;
    QFile f(QDir(dir).filePath("session.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Load session", "No session.json in that folder.");
        return;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonArray vols = manifest["volumes"].toArray();
    if (vols.isEmpty()) return;

    // Reload reference
    auto ref = std::make_unique<ROIVolume>();
    const QJsonObject r0 = vols[0].toObject();
    if (!ref->load(r0["path"].toString().toStdString())) {
        QMessageBox::warning(this, "Load session",
                             "Reference image missing:\n" + r0["path"].toString());
        return;
    }
    ref->setColormap(r0["colormap"].toInt());
    ref->setWindow(r0["wmin"].toDouble(ref->vmin()), r0["wmax"].toDouble(ref->vmax()));

    m_volumes.clear(); m_volVisible.clear(); m_volNames.clear();
    m_volumes.push_back(std::move(ref));
    m_volVisible.push_back(r0["visible"].toBool(true));
    m_volNames.push_back(r0["path"].toString());

    for (int i = 1; i < vols.size(); ++i) {
        const QJsonObject o = vols[i].toObject();
        auto v = std::make_unique<ROIVolume>();
        if (!v->load(o["path"].toString().toStdString())) {
            statusBar()->showMessage("Skipped missing input: " + o["path"].toString());
            continue;
        }
        v->setColormap(o["colormap"].toInt(1));
        v->setFusionAlpha(o["alpha"].toDouble(0.6));
        v->setWindow(o["wmin"].toDouble(v->vmin()), o["wmax"].toDouble(v->vmax()));
        m_volumes.push_back(std::move(v));
        m_volVisible.push_back(o["visible"].toBool(true));
        m_volNames.push_back(o["path"].toString());
    }

    // Restore mask
    const QString mp = QDir(dir).filePath("mask.nii.gz");
    if (QFile::exists(mp)) refVol()->loadMaskRaw(mp.toStdString());

    // Restore SUV params
    const QJsonObject sj = manifest["suv"].toObject();
    SUVParams p;
    p.suvType=sj["suvType"].toInt(); p.weightKg=sj["weightKg"].toDouble(70);
    p.heightCm=sj["heightCm"].toDouble(170); p.sex=sj["sex"].toInt();
    p.doseMbq=sj["doseMbq"].toDouble(370); p.halfLifeS=sj["halfLifeS"].toDouble(F18_HALF_LIFE_S);
    p.decayMin=sj["decayMin"].toDouble(60);
    m_toolPanel->setSuvParams(p);

    m_activeVol = std::min(manifest["active"].toInt(), (int)m_volumes.size() - 1);
    installRefChangeCallback();
    m_toolPanel->setVolume(refVol());
    m_toolPanel->setViewer(m_viewer);
    m_viewer->setVolume(refVol());
    m_viewer->setLayoutPreset(manifest["preset"].toInt(0));
    syncImageList();
    rebuildFusion();
    pushFusionTarget();
    statusBar()->showMessage(QString("Session loaded — %1 image(s).").arg(m_volumes.size()));
}

// ── Polygon ROI fill ────────────────────────────────────────────────────────────

void MainWindow::onPolygon(int axis, const std::vector<std::array<int,3>>& voxels)
{
    ROIVolume* ref = refVol();
    if (!ref || !ref->isLoaded() || voxels.size() < 3) return;
    const int16_t label = (int16_t)m_toolPanel->activeLabel();
    const int NX = ref->nx(), NY = ref->ny(), NZ = ref->nz();
    int16_t* mask = ref->maskImage()->GetBufferPointer();

    // In-plane (col,row) vertices + constant index + grid dims for this axis.
    std::vector<double> vc, vr;
    int constIdx, nrow, ncol;
    for (const auto& p : voxels) {
        if      (axis == 2) { vc.push_back(p[0]); vr.push_back(p[1]); }  // col=x,row=y
        else if (axis == 1) { vc.push_back(p[0]); vr.push_back(p[2]); }  // col=x,row=z
        else                { vc.push_back(p[1]); vr.push_back(p[2]); }  // col=y,row=z
    }
    if      (axis == 2) { constIdx = voxels[0][2]; nrow = NY; ncol = NX; }
    else if (axis == 1) { constIdx = voxels[0][1]; nrow = NZ; ncol = NX; }
    else                { constIdx = voxels[0][0]; nrow = NZ; ncol = NY; }

    const int c0 = std::max(0, (int)*std::min_element(vc.begin(), vc.end()));
    const int c1 = std::min(ncol, (int)*std::max_element(vc.begin(), vc.end()) + 1);
    const int r0 = std::max(0, (int)*std::min_element(vr.begin(), vr.end()));
    const int r1 = std::min(nrow, (int)*std::max_element(vr.begin(), vr.end()) + 1);
    if (c1 <= c0 || r1 <= r0) return;

    ref->pushUndoAll();
    const int n = (int)vc.size();
    long count = 0;
    for (int rr = r0; rr < r1; ++rr)
    for (int cc = c0; cc < c1; ++cc) {
        const double px = cc + 0.5, py = rr + 0.5;
        bool inside = false;                       // even-odd ray casting
        for (int i = 0, j = n - 1; i < n; j = i++) {
            if (((vr[i] > py) != (vr[j] > py)) &&
                (px < (vc[j] - vc[i]) * (py - vr[i]) / (vr[j] - vr[i]) + vc[i]))
                inside = !inside;
        }
        if (!inside) continue;
        long lin = (axis == 2) ? (long)cc + NX*rr + (long)NX*NY*constIdx
                 : (axis == 1) ? (long)cc + NX*constIdx + (long)NX*NY*rr
                               : (long)constIdx + NX*cc + (long)NX*NY*rr;
        mask[lin] = label; ++count;
    }
    ref->notifyChange();
    rebuildFusion();
    m_toolPanel->refreshStats();
    statusBar()->showMessage(QString("Polygon filled %1 voxels (label %2).").arg(count).arg(label));
}
