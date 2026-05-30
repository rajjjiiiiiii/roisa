#pragma once
// ToolPanel.h — Right-side control panel

#include <QWidget>
#include <QString>

class QComboBox; class QSpinBox; class QDoubleSpinBox;
class QCheckBox; class QSlider; class QPushButton;
class QLineEdit; class QLabel; class QStackedWidget;
class QGroupBox;
class ROIVolume; class OrthoViewer;

class ToolPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ToolPanel(QWidget* parent = nullptr);

    void setVolume  (ROIVolume*   vol);
    void setViewer  (OrthoViewer* viewer);

    int     activeLabel() const;
    QString toolMode()    const;

    // Brush settings — read by MainWindow during paint/erase strokes
    int  brushRadius() const;
    int  brushShape()  const;   // 0=sphere 1=cylinder 2=cube
    bool twoDOnly()    const;

signals:
    void refreshRequested();

public slots:
    void onPositionChanged(int x, int y, int z);
    void onSeedSet        (int x, int y, int z);

private slots:
    void applySegmentation();
    void applyMorph();
    void onUndo();
    void onClearLabel();
    void onClearAll();
    void onSaveMask();
    void onLoadMask();
    void onSegMethodChanged(int idx);
    void onNavXChanged(int x);
    void onNavYChanged(int y);
    void onNavZChanged(int z);

private:
    ROIVolume*   m_vol   {nullptr};
    OrthoViewer* m_viewer{nullptr};
    int  m_seedX{0}, m_seedY{0}, m_seedZ{0};
    bool m_seedSet{false};

    // ── Widgets ───────────────────────────────────────────────────────────────
    QComboBox*      m_toolCombo;
    QComboBox*      m_labelCombo;
    QSpinBox*       m_brushRadiusSpin;
    QComboBox*      m_brushShapeCombo;
    QCheckBox*      m_twoDCheckbox;

    QSlider*  m_xSlider, *m_ySlider, *m_zSlider;
    QLabel*   m_xLabel,  *m_yLabel,  *m_zLabel;

    QComboBox*      m_segMethodCombo;
    QStackedWidget* m_segParamStack;

    // Per-method param widgets
    struct { QDoubleSpinBox* lower; QDoubleSpinBox* upper;
             QCheckBox* sliceOnly; QComboBox* sliceAxis; } m_thresh;
    struct { QDoubleSpinBox* tolerance; } m_grow;
    struct { QDoubleSpinBox* lower; QDoubleSpinBox* upper; QSpinBox* radius; } m_nbr;
    struct { QDoubleSpinBox* multiplier; QSpinBox* iterations; QSpinBox* radius; } m_conf;
    struct { QDoubleSpinBox* tolerance; QComboBox* axis; } m_ff2d;
    struct { QDoubleSpinBox* stoppingValue; } m_fm;
    struct { QSpinBox* bins; QSpinBox* classes; } m_otsu;
    struct { QSpinBox* k; } m_kmeans;
    struct { QSpinBox* iterations; QDoubleSpinBox* propagation; QDoubleSpinBox* curvature; } m_ls;
    struct { QSpinBox* minSize; } m_minSize;
    struct { QSpinBox* maxComp; } m_cc;
    struct { QComboBox* axis; } m_fill;
    struct { QSpinBox* thickness; } m_shell;
    struct { QDoubleSpinBox* sigma; } m_smooth;
    struct { QComboBox* labelB; QComboBox* op; } m_bool;

    QLabel*      m_seedLabel;
    QPushButton* m_applySegBtn;

    QSlider*     m_erodeDilateSlider;
    QPushButton* m_applyMorphBtn;

    QPushButton* m_undoBtn, *m_clearLabelBtn, *m_clearAllBtn;

    QLineEdit*   m_saveDirEdit, *m_saveFileEdit, *m_loadMaskEdit;
    QPushButton* m_saveMaskBtn, *m_loadMaskBtn;

    QLabel* m_statusLabel;

    QGroupBox* buildToolGroup();
    QGroupBox* buildNavGroup();
    QGroupBox* buildSegGroup();
    QGroupBox* buildMorphGroup();
    QGroupBox* buildEditGroup();
    QGroupBox* buildIOGroup();

    void setStatus(const QString& msg);
};
