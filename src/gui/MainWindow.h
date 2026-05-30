#pragma once
// MainWindow.h — Top-level application window

#include <QMainWindow>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>
#include "../core/ROIVolume.h"

class OrthoViewer;
class ToolPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Called from main() for command-line path argument
    void loadPath(const QString& path);

private slots:
    void openImage();
    void openDicom();
    void onSeedOrPaint(int x, int y, int z);   // viewer click/drag
    void onMouseReleased();                     // viewer button release

private:
    std::unique_ptr<ROIVolume> m_vol;
    OrthoViewer* m_viewer;
    ToolPanel*   m_toolPanel;

    // Stroke-level undo: first-touch old value per voxel during one drag gesture
    std::unordered_map<int,int16_t> m_strokeFirst;
    bool m_inStroke{false};

    void buildMenus();
    void afterLoad();
    void brushFootprint(int cx, int cy, int cz, int radius, int shape,
                        int viewAxis, bool twoD,
                        std::vector<std::array<int,3>>& out) const;
};
