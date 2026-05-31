#pragma once
// ToolPanel.h — Operator-based right-side control panel
//
//  Operator 0  "Navigation Viewer"   tabs: Viewer | Data Manager | DICOM Tags
//  Operator 1  "ROI"                 tabs: Paint  | Segment      | Labels | Save
//  Operator 2  "Measure"             (single scrollable page)

#include <QWidget>
#include <QString>
#include <QList>
#include <QPair>

class QComboBox;    class QSpinBox;      class QDoubleSpinBox;
class QCheckBox;    class QSlider;       class QPushButton;
class QLineEdit;    class QLabel;        class QStackedWidget;
class QGroupBox;    class QTableWidget;  class QTabWidget;
class HistogramWidget;
class DicomTagWidget;
class ROIVolume;    class OrthoViewer;

class ToolPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ToolPanel(QWidget* parent = nullptr);

    void setVolume  (ROIVolume*   vol);
    void setViewer  (OrthoViewer* viewer);

    int     activeLabel() const;
    QString toolMode()    const;  // "paint"|"erase"|"segment"|"measure"|""

    // Brush settings — read by MainWindow during paint/erase strokes
    int  brushRadius() const;
    int  brushShape()  const;   // 0=sphere  1=cylinder  2=cube
    bool twoDOnly()    const;

    // Called by MainWindow after mask changes
    void refreshStats();
    void onVolumeLoaded();

    // ── Fusion: load the selected layer's display params into the controls ──────
    void setFusionTarget(const QString& name, int colormap, float alpha,
                         float wmin, float wmax, bool isBase, bool baseVisible);

    // ── Registration: populate moving-image dropdown / set status ───────────────
    void setMovingImages(const QList<QPair<QString,int>>& items);
    void setRegStatus(const QString& msg);

signals:
    void refreshRequested();
    // Fusion controls target the layer selected in the Images panel
    void fusionColormapChanged(int cm);
    void fusionAlphaChanged(float alpha);
    void fusionWindowChanged(float lo, float hi);
    void baseVisibleToggled(bool on);
    // Registration operator (movingIdx indexes the loaded inputs, 1-based)
    void registerRequested(int movingIdx, const QString& mode, int iterations);
    void manualTransformRequested(int movingIdx, double tx, double ty, double tz,
                                  double rx, double ry, double rz);
    void resetRegistrationRequested(int movingIdx);

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
    void onOperatorChanged(int idx);
    void onSnapshotView(int viewIdx);   // 0=Sag  1=Cor  2=Axi  3=VTK
    void onExportFrames();
    void onMakeMovie();

private:
    ROIVolume*   m_vol   {nullptr};
    OrthoViewer* m_viewer{nullptr};
    int  m_seedX{0}, m_seedY{0}, m_seedZ{0};
    bool m_seedSet{false};

    // ── Operator selector ─────────────────────────────────────────────────────
    QComboBox*      m_operatorCombo{nullptr};
    QStackedWidget* m_operatorStack{nullptr};

    // ── Paint tab — Tool & Label ───────────────────────────────────────────────
    QComboBox*   m_toolCombo{nullptr};
    QComboBox*   m_labelCombo{nullptr};
    QSpinBox*    m_brushRadiusSpin{nullptr};
    QComboBox*   m_brushShapeCombo{nullptr};
    QCheckBox*   m_twoDCheckbox{nullptr};

    // ── Viewer tab — Navigation sliders ───────────────────────────────────────
    QSlider*  m_xSlider{nullptr}, *m_ySlider{nullptr}, *m_zSlider{nullptr};
    QLabel*   m_xLabel{nullptr},  *m_yLabel{nullptr},  *m_zLabel{nullptr};

    // ── Data Manager — Window / Level ──────────────────────────────────────────
    QDoubleSpinBox* m_wlMinSpin{nullptr};
    QDoubleSpinBox* m_wlMaxSpin{nullptr};
    QComboBox*      m_wlPresetCombo{nullptr};

    // ── Data Manager — Display ─────────────────────────────────────────────────
    QComboBox*   m_colormapCombo{nullptr};
    QSlider*     m_alphaSlider{nullptr};
    QCheckBox*   m_interpolateCheck{nullptr};
    QCheckBox*   m_infoOverlayCheck{nullptr};
    QPushButton* m_showAllBtn{nullptr};
    QPushButton* m_hideAllBtn{nullptr};
    QPushButton* m_resetZoomBtn{nullptr};

    // ── Viewer tab — Cine player ───────────────────────────────────────────────
    QPushButton* m_cinePlayBtn{nullptr};
    QSpinBox*    m_cineFpsSpin{nullptr};
    QComboBox*   m_cineAxisCombo{nullptr};

    // ── Viewer tab — Export ────────────────────────────────────────────────────
    QPushButton* m_snapSagBtn{nullptr};
    QPushButton* m_snapCorBtn{nullptr};
    QPushButton* m_snapAxiBtn{nullptr};
    QPushButton* m_snapVtkBtn{nullptr};
    QComboBox*   m_exportAxisCombo{nullptr};
    QPushButton* m_exportFramesBtn{nullptr};
    QPushButton* m_makeMovieBtn{nullptr};

    // ── Segment tab — Segmentation methods ────────────────────────────────────
    QComboBox*      m_segMethodCombo{nullptr};
    QStackedWidget* m_segParamStack{nullptr};

    struct { QDoubleSpinBox* lower{nullptr}; QDoubleSpinBox* upper{nullptr};
             QCheckBox* sliceOnly{nullptr};  QComboBox* sliceAxis{nullptr}; } m_thresh;
    struct { QDoubleSpinBox* tolerance{nullptr}; } m_grow;
    struct { QDoubleSpinBox* lower{nullptr}; QDoubleSpinBox* upper{nullptr};
             QSpinBox* radius{nullptr}; } m_nbr;
    struct { QDoubleSpinBox* multiplier{nullptr}; QSpinBox* iterations{nullptr};
             QSpinBox* radius{nullptr}; } m_conf;
    struct { QDoubleSpinBox* tolerance{nullptr}; QComboBox* axis{nullptr}; } m_ff2d;
    struct { QDoubleSpinBox* stoppingValue{nullptr}; } m_fm;
    struct { QSpinBox* bins{nullptr};    QSpinBox* classes{nullptr}; } m_otsu;
    struct { QSpinBox* k{nullptr}; } m_kmeans;
    struct { QSpinBox* iterations{nullptr}; QDoubleSpinBox* propagation{nullptr};
             QDoubleSpinBox* curvature{nullptr}; } m_ls;
    struct { QSpinBox* minSize{nullptr}; } m_minSize;
    struct { QSpinBox* maxComp{nullptr}; } m_cc;
    struct { QComboBox* axis{nullptr}; } m_fill;
    struct { QSpinBox* thickness{nullptr}; } m_shell;
    struct { QDoubleSpinBox* sigma{nullptr}; } m_smooth;
    struct { QComboBox* labelB{nullptr}; QComboBox* op{nullptr}; } m_bool;

    QLabel*      m_seedLabel{nullptr};
    QPushButton* m_applySegBtn{nullptr};

    // ── Segment tab — Morph ────────────────────────────────────────────────────
    QSlider*     m_erodeDilateSlider{nullptr};
    QPushButton* m_applyMorphBtn{nullptr};

    // ── Paint tab — Edit ───────────────────────────────────────────────────────
    QPushButton* m_undoBtn{nullptr};
    QPushButton* m_clearLabelBtn{nullptr};
    QPushButton* m_clearAllBtn{nullptr};

    // ── Labels tab ─────────────────────────────────────────────────────────────
    QPushButton* m_centroidBtn{nullptr};
    QPushButton* m_propFwdBtn{nullptr};
    QPushButton* m_propBwdBtn{nullptr};
    QComboBox*   m_propAxisCombo{nullptr};
    QPushButton* m_updateSurfBtn{nullptr};
    QTableWidget* m_statsTable{nullptr};
    QPushButton*  m_csvBtn{nullptr};

    // ── Save tab ───────────────────────────────────────────────────────────────
    QLineEdit*   m_saveDirEdit{nullptr};
    QLineEdit*   m_saveFileEdit{nullptr};
    QLineEdit*   m_loadMaskEdit{nullptr};
    QPushButton* m_saveMaskBtn{nullptr};
    QPushButton* m_loadMaskBtn{nullptr};
    QLineEdit*   m_regMovingEdit{nullptr};
    QPushButton* m_regBtn{nullptr};

    // ── Data Manager — 3D Render ───────────────────────────────────────────────
    QComboBox*      m_vtkModeCombo{nullptr};
    QPushButton*    m_vtkResetCamBtn{nullptr};
    QDoubleSpinBox* m_isoSpacingSpin{nullptr};
    QPushButton*    m_resampleIsoBtn{nullptr};

    // ── Data Manager — Histogram ───────────────────────────────────────────────
    HistogramWidget* m_histWidget{nullptr};

    // ── Data Manager — Fusion (per selected layer) ─────────────────────────────
    QLabel*         m_fusionTargetLabel{nullptr};
    QComboBox*      m_fusionColormap{nullptr};
    QSlider*        m_fusionAlpha{nullptr};
    QLabel*         m_fusionAlphaLabel{nullptr};
    QDoubleSpinBox* m_fusionWmin{nullptr};
    QDoubleSpinBox* m_fusionWmax{nullptr};
    QCheckBox*      m_baseVisibleCheck{nullptr};
    bool            m_fusionLoading{false};

    // ── DICOM Tags tab ─────────────────────────────────────────────────────────
    DicomTagWidget*  m_tagWidget{nullptr};

    // ── Registration operator ───────────────────────────────────────────────────
    QComboBox*      m_regMovingCombo{nullptr};
    QComboBox*      m_regModeCombo{nullptr};
    QSpinBox*       m_regItersSpin{nullptr};
    QPushButton*    m_regRunBtn{nullptr};
    QLabel*         m_regStatusLabel{nullptr};
    QDoubleSpinBox* m_manTx{nullptr}; QDoubleSpinBox* m_manTy{nullptr}; QDoubleSpinBox* m_manTz{nullptr};
    QDoubleSpinBox* m_manRx{nullptr}; QDoubleSpinBox* m_manRy{nullptr}; QDoubleSpinBox* m_manRz{nullptr};
    QPushButton*    m_manApplyBtn{nullptr};
    QPushButton*    m_manResetBtn{nullptr};

    // ── Measure operator ───────────────────────────────────────────────────────
    QComboBox*   m_measureTypeCombo{nullptr};
    QPushButton* m_clearMeasBtn{nullptr};
    QLabel*      m_lastMeasLabel{nullptr};

    QLabel* m_statusLabel{nullptr};

    // ── Group builders ────────────────────────────────────────────────────────
    QGroupBox* buildToolGroup();
    QGroupBox* buildNavGroup();
    QGroupBox* buildWindowLevelGroup();
    QGroupBox* buildDisplayGroup();
    QGroupBox* buildFusionGroup();
    QGroupBox* buildSegGroup();
    QGroupBox* buildMorphGroup();
    QGroupBox* buildEditGroup();
    QGroupBox* buildLabelToolsGroup();
    QGroupBox* buildStatsGroup();
    QGroupBox* buildIOGroup();
    QGroupBox* buildRegistrationGroup();
    QGroupBox* build3DGroup();
    QGroupBox* buildCineGroup();
    QGroupBox* buildMeasureGroup();
    QGroupBox* buildExportGroup();
    QGroupBox* buildAutoRegGroup();
    QGroupBox* buildManualRegGroup();

    // ── Operator page builders ─────────────────────────────────────────────────
    QWidget* buildNavViewerOperator();
    QWidget* buildROIOperator();
    QWidget* buildRegistrationOperator();
    QWidget* buildMeasureOperator();

    void setStatus(const QString& msg);
};
