#pragma once
// ToolPanel.h — Right-side control panel

#include <QWidget>
#include <QString>

class QComboBox; class QSpinBox; class QDoubleSpinBox;
class QCheckBox; class QSlider; class QPushButton;
class QLineEdit; class QLabel; class QStackedWidget;
class QGroupBox; class QTableWidget; class QTabWidget;
class HistogramWidget;
class DicomTagWidget;
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

    // Called by MainWindow after mask changes
    void refreshStats();
    void onVolumeLoaded();

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
    void onPropagateLabel(int direction);
    void onSnapToCentroid();
    void onExportCSV();
    void onResetWindow();
    void onUpdateSurface();
    void onRegisterImage();
    void onResampleIsotropic();
    void onMeasureTypeChanged(int idx);
    void onToolModeChanged(int idx);

private:
    ROIVolume*   m_vol   {nullptr};
    OrthoViewer* m_viewer{nullptr};
    int  m_seedX{0}, m_seedY{0}, m_seedZ{0};
    bool m_seedSet{false};

    // ── Tool & Label ──────────────────────────────────────────────────────────
    QComboBox*      m_toolCombo;
    QComboBox*      m_labelCombo;
    QSpinBox*       m_brushRadiusSpin;
    QComboBox*      m_brushShapeCombo;
    QCheckBox*      m_twoDCheckbox;

    // ── Navigation ────────────────────────────────────────────────────────────
    QSlider*  m_xSlider, *m_ySlider, *m_zSlider;
    QLabel*   m_xLabel,  *m_yLabel,  *m_zLabel;

    // ── Window / Level ────────────────────────────────────────────────────────
    QDoubleSpinBox* m_wlMinSpin;
    QDoubleSpinBox* m_wlMaxSpin;
    QComboBox*      m_wlPresetCombo{nullptr};

    // ── Display ──────────────────────────────────────────────────────────────
    QComboBox*  m_colormapCombo;
    QSlider*    m_alphaSlider;
    QCheckBox*  m_interpolateCheck;
    QCheckBox*  m_infoOverlayCheck{nullptr};
    QPushButton* m_showAllBtn, *m_hideAllBtn, *m_resetZoomBtn;

    // ── Cine player ───────────────────────────────────────────────────────────
    QPushButton* m_cinePlayBtn{nullptr};
    QSpinBox*    m_cineFpsSpin{nullptr};
    QComboBox*   m_cineAxisCombo{nullptr};

    // ── Segmentation ──────────────────────────────────────────────────────────
    QComboBox*      m_segMethodCombo;
    QStackedWidget* m_segParamStack;

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

    // ── Morph ─────────────────────────────────────────────────────────────────
    QSlider*     m_erodeDilateSlider;
    QPushButton* m_applyMorphBtn;

    // ── Edit ──────────────────────────────────────────────────────────────────
    QPushButton* m_undoBtn, *m_clearLabelBtn, *m_clearAllBtn;

    // ── Label tools ───────────────────────────────────────────────────────────
    QPushButton* m_centroidBtn;
    QPushButton* m_propFwdBtn, *m_propBwdBtn;
    QComboBox*   m_propAxisCombo;
    QPushButton* m_updateSurfBtn;

    // ── Stats ─────────────────────────────────────────────────────────────────
    QTableWidget* m_statsTable;
    QPushButton*  m_csvBtn;

    // ── I/O ───────────────────────────────────────────────────────────────────
    QLineEdit*   m_saveDirEdit, *m_saveFileEdit, *m_loadMaskEdit;
    QPushButton* m_saveMaskBtn, *m_loadMaskBtn;

    // ── Registration ──────────────────────────────────────────────────────────
    QLineEdit*   m_regMovingEdit;
    QPushButton* m_regBtn;

    // ── VTK / 3-D controls ────────────────────────────────────────────────────
    QComboBox*      m_vtkModeCombo{nullptr};
    QPushButton*    m_vtkResetCamBtn{nullptr};
    QDoubleSpinBox* m_isoSpacingSpin{nullptr};
    QPushButton*    m_resampleIsoBtn{nullptr};

    // ── Histogram (moved from OrthoViewer) ────────────────────────────────────
    HistogramWidget* m_histWidget{nullptr};

    // ── DICOM tags ────────────────────────────────────────────────────────────
    DicomTagWidget*  m_tagWidget{nullptr};

    // ── Measure controls ──────────────────────────────────────────────────────
    QComboBox*   m_measureTypeCombo{nullptr};
    QPushButton* m_clearMeasBtn{nullptr};
    QLabel*      m_lastMeasLabel{nullptr};

    QTabWidget* m_tabs{nullptr};
    QLabel* m_statusLabel;

    QGroupBox* buildToolGroup();
    QGroupBox* buildNavGroup();
    QGroupBox* buildWindowLevelGroup();
    QGroupBox* buildDisplayGroup();
    QGroupBox* buildSegGroup();
    QGroupBox* buildMorphGroup();
    QGroupBox* buildEditGroup();
    QGroupBox* buildLabelToolsGroup();
    QGroupBox* buildStatsGroup();
    QGroupBox* buildIOGroup();
    QGroupBox* buildRegistrationGroup();
    QGroupBox* build3DGroup();         // VTK render mode + isotropic resample
    QGroupBox* buildCineGroup();       // cine / loop player controls
    QGroupBox* buildMeasureGroup();    // measurement type selector

    void setStatus(const QString& msg);
};
