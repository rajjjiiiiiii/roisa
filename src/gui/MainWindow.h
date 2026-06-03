#pragma once
// MainWindow.h — Top-level application window

#include <QMainWindow>
#include <QSettings>
#include <QHash>
#include <QIcon>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>
#include "../core/ROIVolume.h"
#include "ImageListWidget.h"

class OrthoViewer;
class ToolPanel;
class SeriesBrowser;
class QMenu;
class QAction;
class QActionGroup;
class QProgressBar;
class QLabel;
class QDockWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Called from main() for command-line path argument
    void loadPath(const QString& path);
    void loadDicomSeries(const QString& dir, const QString& uid);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void openImage();
    void openDicom();
    void onSeedOrPaint(int x, int y, int z);
    void onMouseReleased();
    void openRecent();

    // Quantification operator
    void onSuvCompute(int activityIdx);
    void onSuvAutofill(int activityIdx);
    void onSuvExport();
    void onTacCompute(int label, int activityIdx);
    // Analysis tab
    void onPercentThreshold(int sourceLabel, double pct, int targetLabel);
    void onRoiRatio(int labelA, int labelB);
    void onRoiHist(int label);
    // Segmentation/ROI tools
    void onInterpolate(int label, int axis);
    void onThresholdPreview(double lo, double hi, bool on);
    void onKinetic(int target, int input, const QString& model, double dt, int fitFrom);
    // I/O & workflow
    void onExportLabels();
    void onSettings();
    void onGenerateReport();
    void onSaveSession();
    void onLoadSession();
    void onPolygon(int axis, const std::vector<std::array<int,3>>& voxels);

private:
    // ── Multi-image state ─────────────────────────────────────────────────────
    // m_volumes[0] = reference (REF) — all ROI/paint/segment operations use this.
    // m_volumes[1..N] = input images — view-only; switching active is display-only.
    std::vector<std::unique_ptr<ROIVolume>> m_volumes;
    std::vector<bool>                        m_volVisible;
    std::vector<QString>                     m_volNames;
    int                                      m_activeVol{0};

    ROIVolume* refVol()    const;   // m_volumes[0].get()  — for ROI operations
    ROIVolume* activeVol() const;   // m_volumes[m_activeVol].get() — for viewer

    OrthoViewer*     m_viewer;
    ToolPanel*       m_toolPanel;
    ImageListWidget* m_imageList{nullptr};
    SeriesBrowser*   m_browser{nullptr};

    // Stroke-level undo (always on REF)
    std::unordered_map<int,int16_t> m_strokeFirst;
    bool m_inStroke{false};

    // Threshold-preview volume (owned; pointer handed to slice views)
    std::vector<uint8_t> m_previewVol;

    QProgressBar* m_progress{nullptr};
    bool isBusy() const { return m_bgBusy; }   // (segmentation tracked via panel)

    // Recent files
    QMenu*    m_recentMenu{nullptr};
    QSettings m_settings{"ROISA","ROISegmentationAssistant"};
    static constexpr int MAX_RECENT = 10;

    void buildMenus();

    // ── VQ-style chrome: top action bar, left tool rail, status header ─────────
    void    buildToolbars();
    void    selectTool(const QString& key);     // rail click — single source of truth
    void    onPanelToolMode(const QString& mode);// tool changed elsewhere → sync rail
    void    updateStatusHeader();
    void    doUndo();
    static QIcon glyphIcon(const QString& glyph, const QString& color);

    QString                  m_activeTool{"navigate"};
    bool                     m_toolSync{false};
    QActionGroup*            m_toolGroup{nullptr};
    QHash<QString, QAction*> m_toolActs;
    QLabel*                  m_statusHeader{nullptr};
    QDockWidget*             m_controlsDock{nullptr};

    void afterLoad();
    void addRecentFile(const QString& path);
    void rebuildRecentMenu();
    void loadAdditionalImage(const QString& path);
    void activateImage(int idx);
    void removeImage(int idx);
    void syncImageList();
    void installRefChangeCallback();
    void rebuildFusion();       // REF base + each visible input as an overlay
    void pushFusionTarget();    // load selected layer params into ToolPanel

    // Background-task runner — keeps the UI responsive on large volumes.
    // `work` runs on a thread; `onDone` runs on the GUI thread afterward.
    void runBg(std::function<void()> work, std::function<void()> onDone,
               const QString& busyMsg);
    bool m_bgBusy{false};
    void brushFootprint(int cx, int cy, int cz, int radius, int shape,
                        int viewAxis, bool twoD,
                        std::vector<std::array<int,3>>& out) const;
};
