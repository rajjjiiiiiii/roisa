#pragma once
// MainWindow.h — Top-level application window

#include <QMainWindow>
#include <QSettings>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>
#include "../core/ROIVolume.h"

class OrthoViewer;
class ToolPanel;
class SeriesBrowser;
class QMenu;
class QAction;

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

private slots:
    void openImage();
    void openDicom();
    void onSeedOrPaint(int x, int y, int z);
    void onMouseReleased();
    void openRecent();

private:
    std::unique_ptr<ROIVolume> m_vol;
    OrthoViewer*   m_viewer;
    ToolPanel*     m_toolPanel;
    SeriesBrowser* m_browser{nullptr};

    // Stroke-level undo
    std::unordered_map<int,int16_t> m_strokeFirst;
    bool m_inStroke{false};

    // Recent files
    QMenu*    m_recentMenu{nullptr};
    QSettings m_settings{"ROISA","ROISegmentationAssistant"};
    static constexpr int MAX_RECENT = 10;

    void buildMenus();
    void afterLoad();
    void addRecentFile(const QString& path);
    void rebuildRecentMenu();
    void brushFootprint(int cx, int cy, int cz, int radius, int shape,
                        int viewAxis, bool twoD,
                        std::vector<std::array<int,3>>& out) const;
};
