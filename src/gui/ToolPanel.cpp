// ToolPanel.cpp — Operator-based control panel implementation

#include "ToolPanel.h"
#include "BgWorker.h"
#include "DicomTagWidget.h"
#include "HistogramWidget.h"
#include "OrthoViewer.h"
#include "TacWidget.h"
#include "../core/ROIAlgorithms.h"
#include "../core/ROIVolume.h"

#include <QThread>
#include <algorithm>
#include <cmath>
#include <functional>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

// ── Small helpers ─────────────────────────────────────────────────────────────

static QDoubleSpinBox* dbl(double lo,double hi,double val,double step,int dec=2){
    auto* s=new QDoubleSpinBox; s->setRange(lo,hi); s->setValue(val);
    s->setSingleStep(step); s->setDecimals(dec); return s;
}
static QSpinBox* intSpin(int lo,int hi,int val){
    auto* s=new QSpinBox; s->setRange(lo,hi); s->setValue(val); return s;
}

// ── Helper: wrap groups in a scrollable tab page ──────────────────────────────

static QScrollArea* makeTabPage(std::initializer_list<QGroupBox*> groups)
{
    auto* page = new QWidget;
    auto* pl   = new QVBoxLayout(page);
    pl->setContentsMargins(4,4,4,4); pl->setSpacing(4);
    for (auto* g : groups) pl->addWidget(g);
    pl->addStretch();
    auto* sa = new QScrollArea;
    sa->setWidget(page);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    return sa;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════════════════════

ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent)
{
    setMinimumWidth(280); setMaximumWidth(340);
    auto* ml = new QVBoxLayout(this);
    ml->setContentsMargins(2, 2, 2, 2);
    ml->setSpacing(4);

    // ── Operator selector drop-down ───────────────────────────────────────────
    m_operatorCombo = new QComboBox(this);
    m_operatorCombo->addItems({"Navigation Viewer", "ROI", "Registration",
                               "Measure", "Quantification"});
    m_operatorCombo->setStyleSheet(
        "QComboBox{"
        "  background:#1c2a38; color:#9fcfe8; font-weight:bold; font-size:12px;"
        "  padding:5px 10px; border:1px solid #2e5070; border-radius:4px;"
        "}"
        "QComboBox::drop-down{ border:none; width:20px; }"
        "QComboBox QAbstractItemView{"
        "  background:#1c2a38; color:#9fcfe8;"
        "  selection-background-color:#2a5070;"
        "}");
    ml->addWidget(m_operatorCombo);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("border: 1px solid #2a3a4a;");
    ml->addWidget(sep);

    // ── Operator page stack ───────────────────────────────────────────────────
    m_operatorStack = new QStackedWidget(this);
    m_operatorStack->addWidget(buildNavViewerOperator());   // 0
    m_operatorStack->addWidget(buildROIOperator());         // 1
    m_operatorStack->addWidget(buildRegistrationOperator());// 2
    m_operatorStack->addWidget(buildMeasureOperator());     // 3
    m_operatorStack->addWidget(buildQuantOperator());       // 4
    ml->addWidget(m_operatorStack, 1);

    connect(m_operatorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolPanel::onOperatorChanged);

    // ── Status bar ────────────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color:#aaa; font-size:10px; padding:2px 4px;");
    ml->addWidget(m_statusLabel);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Operator page builders
// ═══════════════════════════════════════════════════════════════════════════════

QWidget* ToolPanel::buildNavViewerOperator()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0,0,0,0); l->setSpacing(0);

    auto* tabs = new QTabWidget(w);
    tabs->setDocumentMode(true);

    // ── Tab 0: Viewer — Navigation sliders + Cine player + Export ─────────────
    tabs->addTab(makeTabPage({buildNavGroup(),
                              buildCineGroup(),
                              buildExportGroup()}),
                 "Viewer");

    // ── Tab 1: Data Manager — Histogram + W/L + Display + 3D ─────────────────
    {
        auto* page = new QWidget;
        auto* pl   = new QVBoxLayout(page);
        pl->setContentsMargins(4,4,4,4); pl->setSpacing(4);

        m_histWidget = new HistogramWidget(page);
        m_histWidget->setFixedHeight(90);
        pl->addWidget(m_histWidget);

        pl->addWidget(buildFusionGroup());
        pl->addWidget(buildWindowLevelGroup());
        pl->addWidget(buildDisplayGroup());
        pl->addWidget(build3DGroup());
        pl->addStretch();

        auto* sa = new QScrollArea;
        sa->setWidget(page); sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        tabs->addTab(sa, "Data Manager");
    }

    // ── Tab 2: DICOM Tags ─────────────────────────────────────────────────────
    {
        auto* page = new QWidget;
        auto* pl   = new QVBoxLayout(page);
        pl->setContentsMargins(0,0,0,0); pl->setSpacing(0);
        m_tagWidget = new DicomTagWidget(page);
        pl->addWidget(m_tagWidget);
        tabs->addTab(page, "DICOM Tags");
    }

    l->addWidget(tabs);
    return w;
}

QWidget* ToolPanel::buildROIOperator()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0,0,0,0); l->setSpacing(0);

    auto* tabs = new QTabWidget(w);
    tabs->setDocumentMode(true);

    // ── Tab 0: Paint — brush tool + edit actions ───────────────────────────────
    tabs->addTab(makeTabPage({buildToolGroup(), buildEditGroup()}), "Paint");

    // ── Tab 1: Segment — algorithm selector + morph ───────────────────────────
    tabs->addTab(makeTabPage({buildSegGroup(), buildMorphGroup()}), "Segment");

    // ── Tab 2: Labels — centroid/propagate/stats ──────────────────────────────
    tabs->addTab(makeTabPage({buildLabelToolsGroup(), buildStatsGroup()}), "Labels");

    // ── Tab 3: Save — mask I/O + registration ────────────────────────────────
    tabs->addTab(makeTabPage({buildIOGroup(), buildRegistrationGroup()}), "Save");

    // Auto-switch tool mode to "Segment" when the Segment tab becomes active
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int idx) {
        if (!m_toolCombo || !m_viewer) return;
        if (idx == 1) {
            // Segment tab: force segment tool so clicks set seeds
            QSignalBlocker b(m_toolCombo);
            m_toolCombo->setCurrentIndex(2);   // "Segment"
            m_viewer->setMeasureMode(0);
        } else if (idx == 0) {
            // Paint tab: restore whatever tool is selected and clear measure
            onToolModeChanged(m_toolCombo->currentIndex());
        }
    });

    l->addWidget(tabs);
    return w;
}

QWidget* ToolPanel::buildMeasureOperator()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0,0,0,0); l->setSpacing(0);
    l->addWidget(makeTabPage({buildMeasureGroup()}));
    return w;
}

QWidget* ToolPanel::buildRegistrationOperator()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0,0,0,0); l->setSpacing(0);
    l->addWidget(makeTabPage({buildAutoRegGroup(), buildManualRegGroup()}));
    return w;
}

QWidget* ToolPanel::buildQuantOperator()
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0,0,0,0); l->setSpacing(0);
    l->addWidget(makeTabPage({buildSuvParamGroup(),
                              buildQuantTableGroup(),
                              buildTacGroup()}));
    return w;
}

// ── Quantification groups ───────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildSuvParamGroup()
{
    auto* gb = new QGroupBox("SUV Parameters"); auto* l = new QVBoxLayout(gb);

    auto* arow = new QHBoxLayout; arow->addWidget(new QLabel("Activity image"));
    m_quantImgCombo = new QComboBox;
    m_quantImgCombo->setToolTip("PET / activity-concentration image (Bq/mL)");
    arow->addWidget(m_quantImgCombo, 1); l->addLayout(arow);

    auto* trow = new QHBoxLayout; trow->addWidget(new QLabel("SUV type"));
    m_suvTypeCombo = new QComboBox; m_suvTypeCombo->addItems({"Body Weight", "Lean Body Mass"});
    trow->addWidget(m_suvTypeCombo, 1); l->addLayout(trow);

    auto field = [&](const char* label, QDoubleSpinBox* spin){
        auto* row = new QHBoxLayout; auto* lab = new QLabel(label);
        lab->setFixedWidth(118); row->addWidget(lab); row->addWidget(spin);
        l->addLayout(row);
    };
    m_suvWeight = dbl(1, 500, 70, 1, 1);     field("Weight (kg)", m_suvWeight);
    m_suvHeight = dbl(1, 260, 170, 1, 1);    field("Height (cm)", m_suvHeight);
    m_suvSexCombo = new QComboBox; m_suvSexCombo->addItems({"Male", "Female"});
    auto* srow = new QHBoxLayout; auto* slab = new QLabel("Sex (for LBM)");
    slab->setFixedWidth(118); srow->addWidget(slab); srow->addWidget(m_suvSexCombo, 1);
    l->addLayout(srow);
    m_suvDose  = dbl(0, 100000, 370, 1, 2);  field("Dose (MBq)", m_suvDose);
    m_suvHalf  = dbl(1, 100000, 6586.2, 1, 1); field("Half-life (s)", m_suvHalf);
    m_suvDecay = dbl(0, 1000, 60, 1, 1);     field("Inj→scan (min)", m_suvDecay);

    m_suvAutofillBtn = new QPushButton("Auto-fill from DICOM");
    l->addWidget(m_suvAutofillBtn);
    connect(m_suvAutofillBtn, &QPushButton::clicked, this,
            [this]{ emit suvAutofillRequested(activityIndex()); });
    return gb;
}

QGroupBox* ToolPanel::buildQuantTableGroup()
{
    auto* gb = new QGroupBox("ROI Quantification"); auto* l = new QVBoxLayout(gb);
    m_quantTable = new QTableWidget(0, 6);
    m_quantTable->setHorizontalHeaderLabels(
        {"Label", "Vol (mL)", "SUVmean", "SUVmax", "SUVpeak", "TLG"});
    m_quantTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_quantTable->verticalHeader()->setVisible(false);
    m_quantTable->setMinimumHeight(150);
    l->addWidget(m_quantTable);

    auto* brow = new QHBoxLayout;
    m_quantComputeBtn = new QPushButton("Compute SUV");
    m_quantComputeBtn->setStyleSheet(
        "QPushButton{background:#1c3a55;color:#cfe;font-weight:bold;padding:5px;}"
        "QPushButton:hover{background:#24507a;}");
    m_quantExportBtn = new QPushButton("Export CSV");
    brow->addWidget(m_quantComputeBtn); brow->addWidget(m_quantExportBtn);
    l->addLayout(brow);
    connect(m_quantComputeBtn, &QPushButton::clicked, this,
            [this]{ emit suvComputeRequested(activityIndex()); });
    connect(m_quantExportBtn, &QPushButton::clicked, this,
            [this]{ emit suvExportRequested(); });
    return gb;
}

QGroupBox* ToolPanel::buildTacGroup()
{
    auto* gb = new QGroupBox("Time-Activity Curve  (across loaded frames)");
    auto* l = new QVBoxLayout(gb);
    auto* row = new QHBoxLayout; row->addWidget(new QLabel("Label"));
    m_tacLabelCombo = new QComboBox;
    for (int i = 1; i <= 10; ++i) m_tacLabelCombo->addItem(QString("Label %1").arg(i), i);
    row->addWidget(m_tacLabelCombo, 1);
    m_tacBtn = new QPushButton("Plot TAC");
    row->addWidget(m_tacBtn); l->addLayout(row);
    m_tacWidget = new TacWidget;
    l->addWidget(m_tacWidget);
    connect(m_tacBtn, &QPushButton::clicked, this, [this]{
        emit tacComputeRequested(m_tacLabelCombo->currentData().toInt(), activityIndex());
    });
    return gb;
}

// ── Quantification helpers ──────────────────────────────────────────────────────

void ToolPanel::setQuantImages(const QList<QPair<QString,int>>& items)
{
    if (!m_quantImgCombo) return;
    const QVariant cur = m_quantImgCombo->currentData();
    m_quantImgCombo->blockSignals(true);
    m_quantImgCombo->clear();
    for (const auto& it : items) m_quantImgCombo->addItem(it.first, it.second);
    if (cur.isValid()) {
        const int i = m_quantImgCombo->findData(cur);
        if (i >= 0) m_quantImgCombo->setCurrentIndex(i);
    }
    m_quantImgCombo->blockSignals(false);
}

int ToolPanel::activityIndex() const
{
    if (m_quantImgCombo && m_quantImgCombo->count() > 0) {
        const QVariant d = m_quantImgCombo->currentData();
        return d.isValid() ? d.toInt() : 0;
    }
    return 0;
}

SUVParams ToolPanel::suvParams() const
{
    SUVParams p;
    if (!m_suvTypeCombo) return p;
    p.suvType   = m_suvTypeCombo->currentIndex();
    p.weightKg  = m_suvWeight->value();
    p.heightCm  = m_suvHeight->value();
    p.sex       = m_suvSexCombo->currentIndex();
    p.doseMbq   = m_suvDose->value();
    p.halfLifeS = m_suvHalf->value();
    p.decayMin  = m_suvDecay->value();
    return p;
}

void ToolPanel::setSuvParams(const SUVParams& p)
{
    if (!m_suvTypeCombo) return;
    m_suvTypeCombo->setCurrentIndex(p.suvType);
    m_suvWeight->setValue(p.weightKg);
    m_suvHeight->setValue(p.heightCm);
    m_suvSexCombo->setCurrentIndex(p.sex);
    m_suvDose->setValue(p.doseMbq);
    m_suvHalf->setValue(p.halfLifeS);
    m_suvDecay->setValue(p.decayMin);
}

void ToolPanel::setQuantResults(const std::vector<ROISUVStats>& rows)
{
    m_quantRows = rows;
    m_quantTable->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < (int)rows.size(); ++r) {
        const auto& d = rows[r];
        const QString vals[6] = {
            QString::number(d.label), QString::number(d.volumeMl, 'f', 3),
            QString::number(d.suvMean, 'f', 2), QString::number(d.suvMax, 'f', 2),
            QString::number(d.suvPeak, 'f', 2), QString::number(d.tlg, 'f', 2)};
        for (int c = 0; c < 6; ++c)
            m_quantTable->setItem(r, c, new QTableWidgetItem(vals[c]));
    }
}

void ToolPanel::setTac(const std::vector<double>& values, const QString& ylabel)
{
    if (m_tacWidget) m_tacWidget->setValues(values, ylabel);
}

// ── Registration groups ─────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildAutoRegGroup()
{
    auto* gb = new QGroupBox("Register to Reference"); auto* l = new QVBoxLayout(gb);
    l->addWidget(new QLabel("Fixed: REF (reference image)"));

    auto* mrow = new QHBoxLayout; mrow->addWidget(new QLabel("Moving"));
    m_regMovingCombo = new QComboBox;
    m_regMovingCombo->setToolTip("Input image to align onto the reference");
    mrow->addWidget(m_regMovingCombo, 1); l->addLayout(mrow);

    auto* trow = new QHBoxLayout; trow->addWidget(new QLabel("Transform"));
    m_regModeCombo = new QComboBox;
    m_regModeCombo->addItems({"Rigid", "Affine", "Deformable"});
    trow->addWidget(m_regModeCombo, 1); l->addLayout(trow);

    auto* irow = new QHBoxLayout; irow->addWidget(new QLabel("Iterations"));
    m_regItersSpin = intSpin(10, 1000, 100); irow->addWidget(m_regItersSpin);
    l->addLayout(irow);

    m_regRunBtn = new QPushButton("Register");
    m_regRunBtn->setStyleSheet(
        "QPushButton{background:#1c3a55;color:#cfe;font-weight:bold;padding:5px;}"
        "QPushButton:hover{background:#24507a;}");
    l->addWidget(m_regRunBtn);

    m_regStatusLabel = new QLabel("—");
    m_regStatusLabel->setStyleSheet("color:#9c9;font-size:10px;");
    m_regStatusLabel->setWordWrap(true);
    l->addWidget(m_regStatusLabel);

    connect(m_regRunBtn, &QPushButton::clicked, this, [this]{
        if (!m_regMovingCombo || m_regMovingCombo->count() == 0) {
            setRegStatus("No input image to register. Add one first.");
            return;
        }
        const int movingIdx = m_regMovingCombo->currentData().toInt();
        static const char* MODES[] = {"rigid", "affine", "deformable"};
        const QString mode = MODES[m_regModeCombo->currentIndex()];
        emit registerRequested(movingIdx, mode, m_regItersSpin->value());
    });
    return gb;
}

QGroupBox* ToolPanel::buildManualRegGroup()
{
    auto* gb = new QGroupBox("Manual Adjustment"); auto* l = new QVBoxLayout(gb);
    l->addWidget(new QLabel("Nudge the selected moving image (mm / degrees):"));

    m_manTx = dbl(-200,200,0,1,1); m_manTy = dbl(-200,200,0,1,1); m_manTz = dbl(-200,200,0,1,1);
    m_manRx = dbl(-180,180,0,1,1); m_manRy = dbl(-180,180,0,1,1); m_manRz = dbl(-180,180,0,1,1);
    struct { const char* lbl; QDoubleSpinBox* a; QDoubleSpinBox* b; } rows[] = {
        {"Translate X / Rotate X", m_manTx, m_manRx},
        {"Translate Y / Rotate Y", m_manTy, m_manRy},
        {"Translate Z / Rotate Z", m_manTz, m_manRz},
    };
    for (auto& r : rows) {
        l->addWidget(new QLabel(r.lbl));
        auto* row = new QHBoxLayout; row->addWidget(r.a); row->addWidget(r.b);
        l->addLayout(row);
    }

    auto* brow = new QHBoxLayout;
    m_manApplyBtn = new QPushButton("Apply");
    m_manResetBtn = new QPushButton("Reset");
    brow->addWidget(m_manApplyBtn); brow->addWidget(m_manResetBtn);
    l->addLayout(brow);

    connect(m_manApplyBtn, &QPushButton::clicked, this, [this]{
        if (!m_regMovingCombo || m_regMovingCombo->count() == 0) {
            setRegStatus("Select a moving input first.");
            return;
        }
        const int movingIdx = m_regMovingCombo->currentData().toInt();
        emit manualTransformRequested(movingIdx,
            m_manTx->value(), m_manTy->value(), m_manTz->value(),
            m_manRx->value(), m_manRy->value(), m_manRz->value());
    });
    connect(m_manResetBtn, &QPushButton::clicked, this, [this]{
        for (auto* s : {m_manTx,m_manTy,m_manTz,m_manRx,m_manRy,m_manRz}) s->setValue(0);
        if (m_regMovingCombo && m_regMovingCombo->count() > 0)
            emit resetRegistrationRequested(m_regMovingCombo->currentData().toInt());
    });
    return gb;
}

void ToolPanel::setMovingImages(const QList<QPair<QString,int>>& items)
{
    if (!m_regMovingCombo) return;
    const QVariant cur = m_regMovingCombo->currentData();
    m_regMovingCombo->blockSignals(true);
    m_regMovingCombo->clear();
    for (const auto& it : items)
        m_regMovingCombo->addItem(it.first, it.second);
    if (cur.isValid()) {
        const int i = m_regMovingCombo->findData(cur);
        if (i >= 0) m_regMovingCombo->setCurrentIndex(i);
    }
    m_regMovingCombo->blockSignals(false);
}

void ToolPanel::setRegStatus(const QString& msg)
{
    if (m_regStatusLabel) m_regStatusLabel->setText(msg);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Group builders
// ═══════════════════════════════════════════════════════════════════════════════

// ── Tool group ────────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildToolGroup()
{
    auto* gb=new QGroupBox("Tool & Label"); auto* l=new QVBoxLayout(gb);
    m_toolCombo=new QComboBox;
    // "Measure" is a separate operator — only ROI tools here
    m_toolCombo->addItems({"Paint","Erase","Segment"});
    connect(m_toolCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,&ToolPanel::onToolModeChanged);
    l->addWidget(m_toolCombo);
    m_labelCombo=new QComboBox;
    for(int i=1;i<=10;++i) m_labelCombo->addItem(QString("Label %1").arg(i),i);
    l->addWidget(m_labelCombo);
    auto* row=new QHBoxLayout; row->addWidget(new QLabel("Radius"));
    m_brushRadiusSpin=intSpin(1,20,3); row->addWidget(m_brushRadiusSpin);
    l->addLayout(row);
    m_brushShapeCombo=new QComboBox;
    m_brushShapeCombo->addItems({"Sphere","Cylinder","Cube"}); l->addWidget(m_brushShapeCombo);
    m_twoDCheckbox=new QCheckBox("2D only (current slice)"); l->addWidget(m_twoDCheckbox);
    return gb;
}

// ── Navigation group ──────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildNavGroup()
{
    auto* gb=new QGroupBox("Navigation  (scroll on panel)"); auto* l=new QVBoxLayout(gb);
    auto makeRow=[&](const QString& nm, QSlider*& sl, QLabel*& lbl){
        auto* r=new QHBoxLayout;
        lbl=new QLabel(nm+" 0"); lbl->setFixedWidth(60);
        sl=new QSlider(Qt::Horizontal); sl->setRange(0,255);
        r->addWidget(lbl); r->addWidget(sl); l->addLayout(r);
    };
    makeRow("X (sag)",m_xSlider,m_xLabel);
    makeRow("Y (cor)",m_ySlider,m_yLabel);
    makeRow("Z (axi)",m_zSlider,m_zLabel);
    connect(m_xSlider,&QSlider::valueChanged,this,&ToolPanel::onNavXChanged);
    connect(m_ySlider,&QSlider::valueChanged,this,&ToolPanel::onNavYChanged);
    connect(m_zSlider,&QSlider::valueChanged,this,&ToolPanel::onNavZChanged);
    return gb;
}

// ── Fusion group (per selected layer) ──────────────────────────────────────────

QGroupBox* ToolPanel::buildFusionGroup()
{
    auto* gb = new QGroupBox("Fusion — selected layer");
    auto* l  = new QVBoxLayout(gb);

    m_fusionTargetLabel = new QLabel("Selected: REF (base)");
    m_fusionTargetLabel->setStyleSheet(
        "color:#7ec8ff; font-size:10px; font-weight:bold;");
    l->addWidget(m_fusionTargetLabel);

    auto* cmRow = new QHBoxLayout; cmRow->addWidget(new QLabel("Colormap"));
    m_fusionColormap = new QComboBox;
    m_fusionColormap->addItems({"Gray","Hot","Cool","Viridis"});
    cmRow->addWidget(m_fusionColormap, 1); l->addLayout(cmRow);

    auto* aRow = new QHBoxLayout; aRow->addWidget(new QLabel("Opacity"));
    m_fusionAlpha = new QSlider(Qt::Horizontal);
    m_fusionAlpha->setRange(0,100); m_fusionAlpha->setValue(60);
    m_fusionAlphaLabel = new QLabel("60%"); m_fusionAlphaLabel->setFixedWidth(34);
    aRow->addWidget(m_fusionAlpha, 1); aRow->addWidget(m_fusionAlphaLabel);
    l->addLayout(aRow);

    auto* wRow = new QHBoxLayout; wRow->addWidget(new QLabel("Win"));
    m_fusionWmin = dbl(-1e6, 1e6, 0.0, 1.0, 1);
    m_fusionWmax = dbl(-1e6, 1e6, 1.0, 1.0, 1);
    wRow->addWidget(m_fusionWmin); wRow->addWidget(m_fusionWmax);
    l->addLayout(wRow);

    m_baseVisibleCheck = new QCheckBox("Show reference (base) layer");
    m_baseVisibleCheck->setChecked(true);
    l->addWidget(m_baseVisibleCheck);

    connect(m_fusionColormap, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i){ if(!m_fusionLoading) emit fusionColormapChanged(i); });
    connect(m_fusionAlpha, &QSlider::valueChanged, this, [this](int v){
        m_fusionAlphaLabel->setText(QString("%1%").arg(v));
        if(!m_fusionLoading) emit fusionAlphaChanged(v/100.f);
    });
    auto winChanged = [this]{
        if(!m_fusionLoading)
            emit fusionWindowChanged((float)m_fusionWmin->value(),
                                     (float)m_fusionWmax->value());
    };
    connect(m_fusionWmin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [winChanged](double){ winChanged(); });
    connect(m_fusionWmax, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [winChanged](double){ winChanged(); });
    connect(m_baseVisibleCheck, &QCheckBox::toggled, this, [this](bool on){
        if(!m_fusionLoading) emit baseVisibleToggled(on);
    });
    return gb;
}

void ToolPanel::setFusionTarget(const QString& name, int colormap, float alpha,
                                 float wmin, float wmax, bool isBase,
                                 bool baseVisible)
{
    if (!m_fusionColormap) return;
    m_fusionLoading = true;
    m_fusionTargetLabel->setText(
        QString("Selected: %1%2").arg(name, isBase ? "  (base)" : "  (overlay)"));
    m_fusionColormap->setCurrentIndex(std::max(0, std::min(3, colormap)));
    m_fusionAlpha->setValue((int)std::lround(alpha * 100));
    m_fusionAlphaLabel->setText(QString("%1%").arg((int)std::lround(alpha*100)));
    m_fusionWmin->setValue(wmin);
    m_fusionWmax->setValue(wmax);
    m_fusionAlpha->setEnabled(!isBase);
    m_baseVisibleCheck->setChecked(baseVisible);
    m_fusionLoading = false;
}

// ── Segmentation group ────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildSegGroup()
{
    auto* gb=new QGroupBox("Segmentation"); auto* l=new QVBoxLayout(gb);
    m_segMethodCombo=new QComboBox;
    const QStringList methods={
        "Global Threshold","Region Grow (BFS)","Connected Threshold",
        "Neighborhood Connected","Confidence Connected","Flood Fill 2D",
        "Fast Marching","Otsu Threshold","K-Means","Level Set Refine",
        "Watershed","ROI Connected","Remove Small","Connected Components",
        "Fill Holes","Make Shell","Low Pass Smooth","Boolean Op"
    };
    for(int i=0;i<methods.size();++i) m_segMethodCombo->addItem(methods[i],i);
    l->addWidget(m_segMethodCombo);
    m_seedLabel=new QLabel("Seed: click on image (Segment mode)");
    m_seedLabel->setStyleSheet("color:#f90;font-size:10px;");
    l->addWidget(m_seedLabel);

    m_segParamStack=new QStackedWidget;

    // Page 0: Global Threshold
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_thresh.lower=dbl(-5000,50000,0,10,1);    m_thresh.lower->setPrefix("Lower: ");
      m_thresh.upper=dbl(-5000,50000,1000,10,1); m_thresh.upper->setPrefix("Upper: ");
      m_thresh.sliceOnly=new QCheckBox("Slice only");
      m_thresh.sliceAxis=new QComboBox; m_thresh.sliceAxis->addItems({"Axial(Z)","Coronal(Y)","Sagittal(X)"});
      pl->addWidget(m_thresh.lower); pl->addWidget(m_thresh.upper);
      pl->addWidget(m_thresh.sliceOnly); pl->addWidget(m_thresh.sliceAxis);
      m_segParamStack->addWidget(p); }

    // Page 1: Region Grow
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_grow.tolerance=dbl(0,5000,50,5,1); m_grow.tolerance->setPrefix("Tolerance: ");
      pl->addWidget(m_grow.tolerance); m_segParamStack->addWidget(p); }

    // Page 2: Connected Threshold (reuse thresh widgets)
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      pl->addWidget(new QLabel("Uses Lower/Upper from Global Threshold page."));
      m_segParamStack->addWidget(p); }

    // Page 3: Neighborhood Connected
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_nbr.lower=dbl(-5000,50000,0,10,1);    m_nbr.lower->setPrefix("Lower: ");
      m_nbr.upper=dbl(-5000,50000,1000,10,1); m_nbr.upper->setPrefix("Upper: ");
      m_nbr.radius=intSpin(1,10,1); m_nbr.radius->setPrefix("Nbr radius: ");
      pl->addWidget(m_nbr.lower); pl->addWidget(m_nbr.upper); pl->addWidget(m_nbr.radius);
      m_segParamStack->addWidget(p); }

    // Page 4: Confidence Connected
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_conf.multiplier=dbl(0.5,20,2.5,0.5); m_conf.multiplier->setPrefix("Multiplier: ");
      m_conf.iterations=intSpin(1,50,4);      m_conf.iterations->setPrefix("Iterations: ");
      m_conf.radius    =intSpin(1,10,1);      m_conf.radius->setPrefix("Radius: ");
      pl->addWidget(m_conf.multiplier); pl->addWidget(m_conf.iterations); pl->addWidget(m_conf.radius);
      m_segParamStack->addWidget(p); }

    // Page 5: Flood Fill 2D
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_ff2d.tolerance=dbl(0,5000,50,5,1); m_ff2d.tolerance->setPrefix("Tolerance: ");
      m_ff2d.axis=new QComboBox; m_ff2d.axis->addItems({"Axial(Z)","Coronal(Y)","Sagittal(X)"});
      pl->addWidget(m_ff2d.tolerance); pl->addWidget(m_ff2d.axis);
      m_segParamStack->addWidget(p); }

    // Page 6: Fast Marching
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_fm.stoppingValue=dbl(1,5000,50,5,1); m_fm.stoppingValue->setPrefix("Stopping val: ");
      pl->addWidget(m_fm.stoppingValue); m_segParamStack->addWidget(p); }

    // Page 7: Otsu
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_otsu.bins   =intSpin(16,512,128); m_otsu.bins->setPrefix("Bins: ");
      m_otsu.classes=intSpin(1,8,1);      m_otsu.classes->setPrefix("Classes: ");
      pl->addWidget(m_otsu.bins); pl->addWidget(m_otsu.classes);
      m_segParamStack->addWidget(p); }

    // Page 8: K-Means
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_kmeans.k=intSpin(2,20,3); m_kmeans.k->setPrefix("K: ");
      pl->addWidget(m_kmeans.k); m_segParamStack->addWidget(p); }

    // Page 9: Level Set
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_ls.iterations =intSpin(50,5000,500);   m_ls.iterations->setPrefix("Iterations: ");
      m_ls.propagation=dbl(0,10,1.0,0.1);      m_ls.propagation->setPrefix("Propagation: ");
      m_ls.curvature  =dbl(0,10,1.0,0.1);      m_ls.curvature->setPrefix("Curvature: ");
      pl->addWidget(m_ls.iterations); pl->addWidget(m_ls.propagation); pl->addWidget(m_ls.curvature);
      m_segParamStack->addWidget(p); }

    // Pages 10-11: Watershed / ROI Connected (no params)
    for(int i=10;i<=11;++i){
        auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
        pl->addWidget(new QLabel(i==10?"Splits via morphological watershed.":"Keep component containing seed."));
        m_segParamStack->addWidget(p);
    }

    // Page 12: Remove Small
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_minSize.minSize=intSpin(1,100000,100); m_minSize.minSize->setPrefix("Min voxels: ");
      pl->addWidget(m_minSize.minSize); m_segParamStack->addWidget(p); }

    // Page 13: Connected Components
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_cc.maxComp=intSpin(1,255,255); m_cc.maxComp->setPrefix("Max comps: ");
      pl->addWidget(m_cc.maxComp); m_segParamStack->addWidget(p); }

    // Page 14: Fill Holes
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_fill.axis=new QComboBox;
      m_fill.axis->addItems({"3D (all)","Axial(Z)","Coronal(Y)","Sagittal(X)"});
      pl->addWidget(m_fill.axis); m_segParamStack->addWidget(p); }

    // Page 15: Make Shell
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_shell.thickness=intSpin(1,10,1); m_shell.thickness->setPrefix("Thickness: ");
      pl->addWidget(m_shell.thickness); m_segParamStack->addWidget(p); }

    // Page 16: Low Pass Smooth
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_smooth.sigma=dbl(0.1,10,1.0,0.1); m_smooth.sigma->setPrefix("Sigma: ");
      pl->addWidget(m_smooth.sigma); m_segParamStack->addWidget(p); }

    // Page 17: Boolean Op
    { auto* p=new QWidget; auto* pl=new QVBoxLayout(p);
      m_bool.labelB=new QComboBox;
      for(int i=1;i<=10;++i) m_bool.labelB->addItem(QString("Label %1").arg(i),i);
      m_bool.labelB->setCurrentIndex(1);
      m_bool.op=new QComboBox; m_bool.op->addItems({"or","and","xor","not","subtract"});
      pl->addWidget(new QLabel("Label B:")); pl->addWidget(m_bool.labelB);
      pl->addWidget(new QLabel("Operation:")); pl->addWidget(m_bool.op);
      m_segParamStack->addWidget(p); }

    l->addWidget(m_segParamStack);
    m_applySegBtn=new QPushButton("▶  Apply Segment");
    m_applySegBtn->setStyleSheet("background:#2a6;color:white;font-weight:bold;");
    l->addWidget(m_applySegBtn);

    connect(m_segMethodCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,&ToolPanel::onSegMethodChanged);
    connect(m_applySegBtn,&QPushButton::clicked,this,&ToolPanel::applySegmentation);
    return gb;
}

// ── Morph group ───────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildMorphGroup()
{
    auto* gb=new QGroupBox("Erode / Dilate"); auto* l=new QVBoxLayout(gb);
    l->addWidget(new QLabel("Erode ←   → Dilate"));
    m_erodeDilateSlider=new QSlider(Qt::Horizontal);
    m_erodeDilateSlider->setRange(-5,5); m_erodeDilateSlider->setValue(0);
    m_erodeDilateSlider->setTickInterval(1);
    m_erodeDilateSlider->setTickPosition(QSlider::TicksBelow);
    l->addWidget(m_erodeDilateSlider);
    m_applyMorphBtn=new QPushButton("Apply");
    l->addWidget(m_applyMorphBtn);
    connect(m_applyMorphBtn,&QPushButton::clicked,this,&ToolPanel::applyMorph);
    return gb;
}

// ── Edit group ────────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildEditGroup()
{
    auto* gb=new QGroupBox("Edit"); auto* l=new QHBoxLayout(gb);
    m_undoBtn       =new QPushButton("Undo");
    m_clearLabelBtn =new QPushButton("Clear Label");
    m_clearAllBtn   =new QPushButton("Clear All");
    m_undoBtn->setStyleSheet("background:#a60;");
    m_clearAllBtn->setStyleSheet("background:#900;color:white;");
    l->addWidget(m_undoBtn); l->addWidget(m_clearLabelBtn); l->addWidget(m_clearAllBtn);
    connect(m_undoBtn,      &QPushButton::clicked,this,&ToolPanel::onUndo);
    connect(m_clearLabelBtn,&QPushButton::clicked,this,&ToolPanel::onClearLabel);
    connect(m_clearAllBtn,  &QPushButton::clicked,this,&ToolPanel::onClearAll);
    return gb;
}

// ── I/O group ─────────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildIOGroup()
{
    auto* gb=new QGroupBox("Save / Load Mask"); auto* l=new QVBoxLayout(gb);
    m_saveDirEdit  =new QLineEdit; m_saveDirEdit->setPlaceholderText("/output/dir");
    m_saveFileEdit =new QLineEdit; m_saveFileEdit->setPlaceholderText("roi_mask.nii.gz");
    m_saveMaskBtn  =new QPushButton("Save Mask");
    m_saveMaskBtn->setStyleSheet("background:#262;color:white;");
    auto* bDir=new QPushButton("…"); bDir->setMaximumWidth(30);
    auto* sr=new QHBoxLayout; sr->addWidget(m_saveDirEdit); sr->addWidget(bDir);
    l->addLayout(sr); l->addWidget(m_saveFileEdit); l->addWidget(m_saveMaskBtn);

    m_loadMaskEdit =new QLineEdit; m_loadMaskEdit->setPlaceholderText("/path/to/mask.nii.gz");
    m_loadMaskBtn  =new QPushButton("Load Mask");
    auto* bLoad=new QPushButton("…"); bLoad->setMaximumWidth(30);
    auto* lr=new QHBoxLayout; lr->addWidget(m_loadMaskEdit); lr->addWidget(bLoad);
    l->addLayout(lr); l->addWidget(m_loadMaskBtn);

    connect(bDir,&QPushButton::clicked,this,[this]{
        QString d=QFileDialog::getExistingDirectory(this,"Select output directory");
        if(!d.isEmpty()) m_saveDirEdit->setText(d);
    });
    connect(bLoad,&QPushButton::clicked,this,[this]{
        QString f=QFileDialog::getOpenFileName(this,"Load mask","",
                    "NIfTI (*.nii *.nii.gz);;All files (*)");
        if(!f.isEmpty()) m_loadMaskEdit->setText(f);
    });
    connect(m_saveMaskBtn,&QPushButton::clicked,this,&ToolPanel::onSaveMask);
    connect(m_loadMaskBtn,&QPushButton::clicked,this,&ToolPanel::onLoadMask);
    return gb;
}

// ── Window / Level group ──────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildWindowLevelGroup()
{
    auto* gb=new QGroupBox("Window / Level"); auto* l=new QVBoxLayout(gb);
    auto* row=new QHBoxLayout;
    m_wlMinSpin=new QDoubleSpinBox; m_wlMinSpin->setRange(-100000,100000); m_wlMinSpin->setDecimals(1); m_wlMinSpin->setPrefix("Min ");
    m_wlMaxSpin=new QDoubleSpinBox; m_wlMaxSpin->setRange(-100000,100000); m_wlMaxSpin->setDecimals(1); m_wlMaxSpin->setPrefix("Max ");
    auto* autoBtn=new QPushButton("Auto");
    row->addWidget(m_wlMinSpin); row->addWidget(m_wlMaxSpin); row->addWidget(autoBtn);
    l->addLayout(row);

    static constexpr struct { const char* name; float center, width; } WL_PRESETS[] = {
        {"Brain",            40,    80},
        {"Stroke",            8,    32},
        {"Subdural",         75,   215},
        {"Temporal Bone",   700,  4000},
        {"Soft Tissue",      40,   350},
        {"Mediastinum",      40,   400},
        {"Abdomen",          60,   400},
        {"Lung",           -600,  1500},
        {"Bone",            700,  3000},
        {"MR T1 Brain",     500,  1000},
        {"MR T2 Brain",     800,  1600},
    };
    m_wlPresetCombo = new QComboBox;
    m_wlPresetCombo->addItem("— CT/MR Preset —");
    for (const auto& p : WL_PRESETS)
        m_wlPresetCombo->addItem(
            QString("%1  (W:%2 L:%3)").arg(p.name).arg((int)p.width).arg((int)p.center));
    l->addWidget(m_wlPresetCombo);

    connect(m_wlMinSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this](double v){
        if(m_vol){ m_vol->setWindow((float)v,(float)m_wlMaxSpin->value()); emit refreshRequested();}});
    connect(m_wlMaxSpin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this](double v){
        if(m_vol){ m_vol->setWindow((float)m_wlMinSpin->value(),(float)v); emit refreshRequested();}});
    connect(autoBtn,&QPushButton::clicked,this,&ToolPanel::onResetWindow);
    connect(m_wlPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx){
                if (idx == 0 || !m_vol) return;
                static constexpr float C[] = {40,8,75,700,40,40,60,-600,700,500,800};
                static constexpr float W[] = {80,32,215,4000,350,400,400,1500,3000,1000,1600};
                int i = idx - 1;
                float lo = C[i] - W[i] / 2.f, hi = C[i] + W[i] / 2.f;
                m_vol->setWindow(lo, hi);
                { QSignalBlocker b1(m_wlMinSpin), b2(m_wlMaxSpin);
                  m_wlMinSpin->setValue(lo); m_wlMaxSpin->setValue(hi); }
                emit refreshRequested();
                { QSignalBlocker b3(m_wlPresetCombo); m_wlPresetCombo->setCurrentIndex(0); }
            });
    return gb;
}

// ── Display group ─────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildDisplayGroup()
{
    auto* gb=new QGroupBox("Display"); auto* l=new QVBoxLayout(gb);
    m_colormapCombo=new QComboBox;
    m_colormapCombo->addItems({"Gray","Hot","Cool","Viridis"});
    l->addWidget(m_colormapCombo);
    auto* arow=new QHBoxLayout;
    arow->addWidget(new QLabel("Overlay α"));
    m_alphaSlider=new QSlider(Qt::Horizontal); m_alphaSlider->setRange(0,100); m_alphaSlider->setValue(100);
    arow->addWidget(m_alphaSlider); l->addLayout(arow);
    m_interpolateCheck=new QCheckBox("Smooth interpolation"); l->addWidget(m_interpolateCheck);
    m_infoOverlayCheck=new QCheckBox("Show info overlay (W/L + position)");
    m_infoOverlayCheck->setChecked(true);
    l->addWidget(m_infoOverlayCheck);
    auto* brow=new QHBoxLayout;
    m_showAllBtn=new QPushButton("Show All"); m_hideAllBtn=new QPushButton("Hide All");
    m_resetZoomBtn=new QPushButton("Reset Zoom");
    brow->addWidget(m_showAllBtn); brow->addWidget(m_hideAllBtn); brow->addWidget(m_resetZoomBtn);
    l->addLayout(brow);
    connect(m_colormapCombo,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int idx){
        if(m_viewer) m_viewer->setColormap(idx);});
    connect(m_alphaSlider,&QSlider::valueChanged,this,[this](int v){
        if(m_viewer) m_viewer->setOverlayAlpha(v/100.f);});
    connect(m_interpolateCheck,&QCheckBox::toggled,this,[this](bool on){
        if(m_viewer) m_viewer->setInterpolate(on);});
    connect(m_infoOverlayCheck,&QCheckBox::toggled,this,[this](bool on){
        if(m_viewer) m_viewer->setShowInfoOverlay(on);});
    connect(m_showAllBtn,&QPushButton::clicked,this,[this]{ if(m_viewer) m_viewer->setAllLabelsVisible(true);});
    connect(m_hideAllBtn,&QPushButton::clicked,this,[this]{ if(m_viewer) m_viewer->setAllLabelsVisible(false);});
    connect(m_resetZoomBtn,&QPushButton::clicked,this,[this]{ if(m_viewer) m_viewer->resetAllZoom();});
    return gb;
}

// ── Label tools group ─────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildLabelToolsGroup()
{
    auto* gb=new QGroupBox("Label Tools"); auto* l=new QVBoxLayout(gb);
    m_centroidBtn=new QPushButton("Snap to Centroid"); l->addWidget(m_centroidBtn);
    auto* prow=new QHBoxLayout;
    prow->addWidget(new QLabel("Propagate:"));
    m_propAxisCombo=new QComboBox; m_propAxisCombo->addItems({"X(sag)","Y(cor)","Z(axi)"});
    prow->addWidget(m_propAxisCombo);
    m_propBwdBtn=new QPushButton("◀"); m_propBwdBtn->setFixedWidth(30);
    m_propFwdBtn=new QPushButton("▶"); m_propFwdBtn->setFixedWidth(30);
    prow->addWidget(m_propBwdBtn); prow->addWidget(m_propFwdBtn);
    l->addLayout(prow);
    m_updateSurfBtn=new QPushButton("Update 3D Surface");
    m_updateSurfBtn->setStyleSheet("background:#226;color:white;");
    l->addWidget(m_updateSurfBtn);
    connect(m_centroidBtn,  &QPushButton::clicked,this,&ToolPanel::onSnapToCentroid);
    connect(m_propBwdBtn,   &QPushButton::clicked,this,[this]{ onPropagateLabel(-1);});
    connect(m_propFwdBtn,   &QPushButton::clicked,this,[this]{ onPropagateLabel(+1);});
    connect(m_updateSurfBtn,&QPushButton::clicked,this,&ToolPanel::onUpdateSurface);
    return gb;
}

// ── Stats group ───────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildStatsGroup()
{
    auto* gb=new QGroupBox("Label Statistics"); auto* l=new QVBoxLayout(gb);
    m_statsTable=new QTableWidget(0,5);
    m_statsTable->setHorizontalHeaderLabels({"Label","Voxels","Vol(mm³)","Mean","Std"});
    m_statsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_statsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_statsTable->setMaximumHeight(120);
    m_statsTable->horizontalHeader()->setStretchLastSection(true);
    l->addWidget(m_statsTable);
    m_csvBtn=new QPushButton("Export CSV…"); l->addWidget(m_csvBtn);
    connect(m_csvBtn,&QPushButton::clicked,this,&ToolPanel::onExportCSV);
    return gb;
}

// ── Registration group ────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildRegistrationGroup()
{
    auto* gb=new QGroupBox("Image Registration"); auto* l=new QVBoxLayout(gb);
    l->addWidget(new QLabel("Moving image (NIfTI or DICOM folder):"));
    m_regMovingEdit=new QLineEdit; m_regMovingEdit->setPlaceholderText("/path/to/moving.nii.gz");
    auto* br=new QPushButton("…"); br->setMaximumWidth(30);
    auto* rrow=new QHBoxLayout; rrow->addWidget(m_regMovingEdit); rrow->addWidget(br);
    l->addLayout(rrow);
    m_regBtn=new QPushButton("Register & Load");
    m_regBtn->setStyleSheet("background:#520;color:white;font-weight:bold;");
    l->addWidget(m_regBtn);
    l->addWidget(new QLabel("Rigid registration. Replaces display\nimage; mask preserved."));
    connect(br,&QPushButton::clicked,this,[this]{
        QString p=QFileDialog::getOpenFileName(this,"Select moving image","",
                    "NIfTI (*.nii *.nii.gz);;All files (*)");
        if(!p.isEmpty()) m_regMovingEdit->setText(p);
    });
    connect(m_regBtn,&QPushButton::clicked,this,&ToolPanel::onRegisterImage);
    return gb;
}

// ── Cine group ────────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildCineGroup()
{
    auto* gb = new QGroupBox("Cine / Loop"); auto* l = new QVBoxLayout(gb);

    auto* topRow = new QHBoxLayout;
    m_cinePlayBtn = new QPushButton("▶  Play");
    m_cinePlayBtn->setCheckable(true);
    m_cinePlayBtn->setStyleSheet("QPushButton:checked{background:#245;color:white;}");
    topRow->addWidget(m_cinePlayBtn);
    topRow->addWidget(new QLabel("FPS:"));
    m_cineFpsSpin = new QSpinBox;
    m_cineFpsSpin->setRange(1, 30); m_cineFpsSpin->setValue(8); m_cineFpsSpin->setMaximumWidth(54);
    topRow->addWidget(m_cineFpsSpin);
    l->addLayout(topRow);

    auto* axRow = new QHBoxLayout;
    axRow->addWidget(new QLabel("Axis:"));
    m_cineAxisCombo = new QComboBox;
    m_cineAxisCombo->addItems({"Sagittal (X)", "Coronal (Y)", "Axial (Z)"});
    m_cineAxisCombo->setCurrentIndex(2);
    axRow->addWidget(m_cineAxisCombo);
    l->addLayout(axRow);

    // Connections are wired in setViewer() once the viewer is available
    return gb;
}

// ── Export group ──────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildExportGroup()
{
    auto* gb = new QGroupBox("Export"); auto* l = new QVBoxLayout(gb);

    // ── Snapshots ──────────────────────────────────────────────────────────────
    l->addWidget(new QLabel("Save snapshot PNG:", gb));
    auto* snapRow = new QHBoxLayout;
    m_snapSagBtn = new QPushButton("Sag");
    m_snapCorBtn = new QPushButton("Cor");
    m_snapAxiBtn = new QPushButton("Axi");
    m_snapVtkBtn = new QPushButton("3D");
    for (auto* b : {m_snapSagBtn, m_snapCorBtn, m_snapAxiBtn, m_snapVtkBtn})
        snapRow->addWidget(b);
    l->addLayout(snapRow);
    connect(m_snapSagBtn, &QPushButton::clicked, this, [this]{ onSnapshotView(0); });
    connect(m_snapCorBtn, &QPushButton::clicked, this, [this]{ onSnapshotView(1); });
    connect(m_snapAxiBtn, &QPushButton::clicked, this, [this]{ onSnapshotView(2); });
    connect(m_snapVtkBtn, &QPushButton::clicked, this, [this]{ onSnapshotView(3); });

    // ── Slice series / movie ───────────────────────────────────────────────────
    auto* hRule = new QFrame(gb);
    hRule->setFrameShape(QFrame::HLine);
    hRule->setStyleSheet("color:#333;");
    l->addWidget(hRule);

    l->addWidget(new QLabel("Export slice series / movie:", gb));
    auto* axRow = new QHBoxLayout;
    axRow->addWidget(new QLabel("Axis:", gb));
    m_exportAxisCombo = new QComboBox;
    m_exportAxisCombo->addItems({"Sagittal (X)", "Coronal (Y)", "Axial (Z)"});
    m_exportAxisCombo->setCurrentIndex(2);
    axRow->addWidget(m_exportAxisCombo);
    l->addLayout(axRow);

    m_exportFramesBtn = new QPushButton("Export Frames to Folder…");
    m_exportFramesBtn->setStyleSheet("background:#1e3a52; color:white;");
    l->addWidget(m_exportFramesBtn);

    m_makeMovieBtn = new QPushButton("Export Movie (MP4)…");
    m_makeMovieBtn->setStyleSheet("background:#1e3a52; color:white;");
    l->addWidget(m_makeMovieBtn);

    auto* note = new QLabel("Movie export requires ffmpeg in PATH.", gb);
    note->setStyleSheet("color:#777; font-size:10px;");
    note->setWordWrap(true);
    l->addWidget(note);

    connect(m_exportFramesBtn, &QPushButton::clicked, this, &ToolPanel::onExportFrames);
    connect(m_makeMovieBtn,    &QPushButton::clicked, this, &ToolPanel::onMakeMovie);

    return gb;
}

// ── 3-D / VTK group ───────────────────────────────────────────────────────────

QGroupBox* ToolPanel::build3DGroup()
{
    auto* gb = new QGroupBox("3-D Render"); auto* l = new QVBoxLayout(gb);

    auto* mrow = new QHBoxLayout;
    mrow->addWidget(new QLabel("Mode:"));
    m_vtkModeCombo = new QComboBox;
    m_vtkModeCombo->addItems({"Volume", "Surfaces", "Both"});
    m_vtkModeCombo->setCurrentIndex(2);
    mrow->addWidget(m_vtkModeCombo);
    l->addLayout(mrow);

    m_vtkResetCamBtn = new QPushButton("Reset Camera");
    l->addWidget(m_vtkResetCamBtn);

    l->addWidget(new QLabel("Resample to isotropic voxels:"));
    auto* irow = new QHBoxLayout;
    irow->addWidget(new QLabel("Spacing (mm):"));
    m_isoSpacingSpin = new QDoubleSpinBox;
    m_isoSpacingSpin->setRange(0.0, 10.0);   // 0.0 == special-value "Auto (min)"
    m_isoSpacingSpin->setValue(0.0);
    m_isoSpacingSpin->setSingleStep(0.1);
    m_isoSpacingSpin->setDecimals(3);
    m_isoSpacingSpin->setSpecialValueText("Auto (min)");
    irow->addWidget(m_isoSpacingSpin);
    l->addLayout(irow);

    m_resampleIsoBtn = new QPushButton("Apply Isotropic Resample");
    m_resampleIsoBtn->setStyleSheet("background:#245;color:white;font-weight:bold;");
    l->addWidget(m_resampleIsoBtn);
    auto* hint = new QLabel("Replaces display image; mask is reset.\n"
                            "Recommended before VTK volume render.", gb);
    hint->setStyleSheet("color:#888;font-size:10px;");
    l->addWidget(hint);

    connect(m_vtkModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx){ if(m_viewer) m_viewer->setVtkRenderMode(idx); });
    connect(m_vtkResetCamBtn, &QPushButton::clicked,
            this, [this]{ if(m_viewer) m_viewer->resetVtkCamera(); });
    connect(m_resampleIsoBtn, &QPushButton::clicked,
            this, &ToolPanel::onResampleIsotropic);

    return gb;
}

// ── Measure group ─────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildMeasureGroup()
{
    auto* gb = new QGroupBox("Measurement Tool"); auto* l = new QVBoxLayout(gb);
    l->addWidget(new QLabel(
        "Select the measurement type below,\n"
        "then click in any slice view.", gb));

    l->addWidget(new QLabel("Measurement type:", gb));
    m_measureTypeCombo = new QComboBox;
    m_measureTypeCombo->addItems({"Ruler (2 clicks)",
                                   "Angle (3 clicks — vertex 2nd)",
                                   "Circle area (drag)"});
    l->addWidget(m_measureTypeCombo);

    m_lastMeasLabel = new QLabel("—", gb);
    m_lastMeasLabel->setStyleSheet("color:#aef; font-size:11px;");
    m_lastMeasLabel->setWordWrap(true);
    l->addWidget(new QLabel("Last result:", gb));
    l->addWidget(m_lastMeasLabel);

    m_clearMeasBtn = new QPushButton("Clear All Measurements");
    m_clearMeasBtn->setStyleSheet("background:#500;color:white;");
    l->addWidget(m_clearMeasBtn);

    connect(m_measureTypeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolPanel::onMeasureTypeChanged);
    connect(m_clearMeasBtn, &QPushButton::clicked, this, [this]{
        if(m_viewer) m_viewer->clearMeasurements();
    });
    return gb;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════════

void ToolPanel::setVolume(ROIVolume* vol)
{
    m_vol=vol;
    if(m_tagWidget) m_tagWidget->setVolume(vol);
    if(m_histWidget){
        m_histWidget->setVolume(vol);
        connect(m_histWidget, &HistogramWidget::windowChanged, this,
                [this](float lo, float hi){
                    if(m_vol) m_vol->setWindow(lo, hi);
                    emit refreshRequested();
                }, Qt::UniqueConnection);
    }
    if(!vol||!vol->isLoaded()) return;
    {QSignalBlocker b(m_xSlider); m_xSlider->setMaximum(vol->nx()-1); m_xSlider->setValue(vol->nx()/2); m_xLabel->setText(QString("X %1").arg(vol->nx()/2));}
    {QSignalBlocker b(m_ySlider); m_ySlider->setMaximum(vol->ny()-1); m_ySlider->setValue(vol->ny()/2); m_yLabel->setText(QString("Y %1").arg(vol->ny()/2));}
    {QSignalBlocker b(m_zSlider); m_zSlider->setMaximum(vol->nz()-1); m_zSlider->setValue(vol->nz()/2); m_zLabel->setText(QString("Z %1").arg(vol->nz()/2));}
    float vmin=vol->vmin(), vmax=vol->vmax();
    m_thresh.lower->setRange(vmin,vmax); m_thresh.lower->setValue(vmin);
    m_thresh.upper->setRange(vmin,vmax); m_thresh.upper->setValue(vmax);
    m_nbr.lower->setRange(vmin,vmax);    m_nbr.lower->setValue(vmin);
    m_nbr.upper->setRange(vmin,vmax);    m_nbr.upper->setValue(vmax);
    onVolumeLoaded();
}

void ToolPanel::setViewer(OrthoViewer* v)
{
    m_viewer=v;
    if(v){
        connect(v, &OrthoViewer::measurementAdded, this,
                [this](const QString& s){
                    if(m_lastMeasLabel) m_lastMeasLabel->setText(s);
                }, Qt::UniqueConnection);

        // ── Cine player ───────────────────────────────────────────────────────
        connect(m_cinePlayBtn, &QPushButton::toggled, this, [this](bool on){
            if (!m_viewer) return;
            if (on) {
                m_cinePlayBtn->setText("⏹  Stop");
                m_viewer->setCineFps(m_cineFpsSpin->value());
                m_viewer->setCineAxis(m_cineAxisCombo->currentIndex());
                m_viewer->playCine();
            } else {
                m_cinePlayBtn->setText("▶  Play");
                m_viewer->stopCine();
            }
        }, Qt::UniqueConnection);
        connect(m_cineFpsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int fps){ if(m_viewer) m_viewer->setCineFps(fps); },
                Qt::UniqueConnection);
        connect(m_cineAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int ax){ if(m_viewer) m_viewer->setCineAxis(ax); },
                Qt::UniqueConnection);

        if (m_infoOverlayCheck)
            v->setShowInfoOverlay(m_infoOverlayCheck->isChecked());
    }
}

int     ToolPanel::activeLabel() const { return m_labelCombo->currentData().toInt(); }
int     ToolPanel::brushRadius() const { return m_brushRadiusSpin ? m_brushRadiusSpin->value() : 3; }
int     ToolPanel::brushShape()  const { return m_brushShapeCombo ? m_brushShapeCombo->currentIndex() : 0; }
bool    ToolPanel::twoDOnly()    const { return m_twoDCheckbox    ? m_twoDCheckbox->isChecked() : false; }

QString ToolPanel::toolMode() const
{
    if (m_operatorCombo) {
        const int op = m_operatorCombo->currentIndex();
        if (op == 0) return "";          // Navigation Viewer — no painting
        if (op == 2) return "";          // Registration — no painting
        if (op == 3) return "measure";   // Measure operator
        if (op == 4) return "";          // Quantification — no painting
    }
    // ROI operator (index 1) — derive from tool combo
    return m_toolCombo ? m_toolCombo->currentText().toLower() : "paint";
}

void ToolPanel::onPositionChanged(int x,int y,int z){
    QSignalBlocker bx(m_xSlider),by(m_ySlider),bz(m_zSlider);
    m_xSlider->setValue(x); m_xLabel->setText(QString("X %1").arg(x));
    m_ySlider->setValue(y); m_yLabel->setText(QString("Y %1").arg(y));
    m_zSlider->setValue(z); m_zLabel->setText(QString("Z %1").arg(z));
}

void ToolPanel::onSeedSet(int x,int y,int z){
    m_seedX=x; m_seedY=y; m_seedZ=z; m_seedSet=true;
    m_seedLabel->setText(
        QString("Seed: (%1,%2,%3)  val: %4")
            .arg(x).arg(y).arg(z)
            .arg(m_vol?QString::number(m_vol->getIntensity(x,y,z),'f',1):"?"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Slot implementations
// ═══════════════════════════════════════════════════════════════════════════════

void ToolPanel::onNavXChanged(int x){ m_xLabel->setText(QString("X %1").arg(x)); if(m_viewer) m_viewer->setX(x); }
void ToolPanel::onNavYChanged(int y){ m_yLabel->setText(QString("Y %1").arg(y)); if(m_viewer) m_viewer->setY(y); }
void ToolPanel::onNavZChanged(int z){ m_zLabel->setText(QString("Z %1").arg(z)); if(m_viewer) m_viewer->setZ(z); }
void ToolPanel::onSegMethodChanged(int idx){ m_segParamStack->setCurrentIndex(idx); }

void ToolPanel::onOperatorChanged(int idx)
{
    m_operatorStack->setCurrentIndex(idx);
    if (!m_viewer) return;

    if (idx == 3) {
        // Measure operator — activate measure tool
        int type = m_measureTypeCombo ? m_measureTypeCombo->currentIndex() + 1 : 1;
        m_viewer->setMeasureMode(type);
    } else {
        // Navigation Viewer / ROI / Registration — disable measure mode
        m_viewer->setMeasureMode(0);
        if (idx == 1 && m_toolCombo)
            onToolModeChanged(m_toolCombo->currentIndex());
    }
}

void ToolPanel::onToolModeChanged(int /*idx*/)
{
    if (!m_viewer) return;
    // All ROI tools (paint/erase/segment) clear the measure overlay
    m_viewer->setMeasureMode(0);
}

void ToolPanel::onMeasureTypeChanged(int idx)
{
    // Only apply when Measure operator is active
    if (m_operatorCombo && m_operatorCombo->currentIndex() == 3 && m_viewer)
        m_viewer->setMeasureMode(idx + 1);   // 0=None, 1=Ruler, 2=Angle, 3=Circle
}

// ── Apply segmentation ────────────────────────────────────────────────────────

void ToolPanel::applySegmentation()
{
    if(!m_vol||!m_vol->isLoaded()){ setStatus("No image loaded."); return; }
    static const int SEED_METHODS[]={1,2,3,4,5,6,11};
    int method=m_segMethodCombo->currentData().toInt();
    for(int sm:SEED_METHODS) if(sm==method&&!m_seedSet){
        setStatus("Switch to Segment tool and click on image to set a seed first."); return; }
    if (m_segRunning) { setStatus("Segmentation already running…"); return; }
    int16_t lbl=static_cast<int16_t>(activeLabel());
    ROIVolume* vol = m_vol;

    // Capture all parameters now (GUI thread) into a self-contained operation.
    std::function<void()> op;
    switch(method){
    case 0:{int axis=-1,si=-1;
        if(m_thresh.sliceOnly->isChecked()){
            int ac=m_thresh.sliceAxis->currentIndex();
            axis=(ac==0)?2:(ac==1)?1:0;
            if(m_viewer) si=(axis==2)?m_viewer->z():(axis==1)?m_viewer->y():m_viewer->x();
        }
        float lo=(float)m_thresh.lower->value(), hi=(float)m_thresh.upper->value();
        op=[=]{ ROIAlgorithms::thresholdSegment(*vol,lo,hi,lbl,axis,si); }; break;}
    case 1:{float tol=(float)m_grow.tolerance->value(); int sx=m_seedX,sy=m_seedY,sz=m_seedZ;
        op=[=]{ ROIAlgorithms::regionGrow(*vol,sx,sy,sz,tol,lbl); }; break;}
    case 2:{float lo=(float)m_thresh.lower->value(),hi=(float)m_thresh.upper->value(); int sx=m_seedX,sy=m_seedY,sz=m_seedZ;
        op=[=]{ ROIAlgorithms::connectedThreshold(*vol,sx,sy,sz,lo,hi,lbl); }; break;}
    case 3:{float lo=(float)m_nbr.lower->value(),hi=(float)m_nbr.upper->value(); int r=m_nbr.radius->value(),sx=m_seedX,sy=m_seedY,sz=m_seedZ;
        op=[=]{ ROIAlgorithms::neighborhoodConnected(*vol,sx,sy,sz,lo,hi,r,lbl); }; break;}
    case 4:{float mul=(float)m_conf.multiplier->value(); int it=m_conf.iterations->value(),r=m_conf.radius->value(),sx=m_seedX,sy=m_seedY,sz=m_seedZ;
        op=[=]{ ROIAlgorithms::confidenceConnected(*vol,sx,sy,sz,mul,it,r,lbl); }; break;}
    case 5:{int ac=m_ff2d.axis->currentIndex(); int ax=(ac==0)?2:(ac==1)?1:0; float tol=(float)m_ff2d.tolerance->value(); int sx=m_seedX,sy=m_seedY,sz=m_seedZ;
        op=[=]{ ROIAlgorithms::floodFill2D(*vol,sx,sy,sz,ax,tol,lbl); }; break;}
    case 6:{float sv=(float)m_fm.stoppingValue->value(); int sx=m_seedX,sy=m_seedY,sz=m_seedZ;
        op=[=]{ ROIAlgorithms::fastMarching(*vol,sx,sy,sz,sv,lbl); }; break;}
    case 7:{int b=m_otsu.bins->value(),c=m_otsu.classes->value();
        op=[=]{ ROIAlgorithms::otsuThreshold(*vol,lbl,b,c); }; break;}
    case 8:{int k=m_kmeans.k->value();
        op=[=]{ ROIAlgorithms::kMeansCluster(*vol,k,lbl); }; break;}
    case 9:{int it=m_ls.iterations->value(); float pr=(float)m_ls.propagation->value(),cu=(float)m_ls.curvature->value();
        op=[=]{ ROIAlgorithms::levelSetRefine(*vol,lbl,it,pr,cu); }; break;}
    case 10: op=[=]{ ROIAlgorithms::watershed(*vol,lbl); }; break;
    case 11:{int sx=m_seedX,sy=m_seedY,sz=m_seedZ;
        op=[=]{ ROIAlgorithms::roiConnected(*vol,sx,sy,sz,lbl,lbl); }; break;}
    case 12:{int ms=m_minSize.minSize->value();
        op=[=]{ ROIAlgorithms::removeSmallComponents(*vol,lbl,ms); }; break;}
    case 13:{int mc=m_cc.maxComp->value();
        op=[=]{ ROIAlgorithms::connectedComponents(*vol,lbl,mc); }; break;}
    case 14:{int ac=m_fill.axis->currentIndex(); int ax=(ac==0)?-1:(ac==1)?2:(ac==2)?1:0;
        op=[=]{ ROIAlgorithms::fillHoles(*vol,lbl,ax); }; break;}
    case 15:{int th=m_shell.thickness->value();
        op=[=]{ ROIAlgorithms::makeShell(*vol,lbl,th); }; break;}
    case 16:{float sg=(float)m_smooth.sigma->value();
        op=[=]{ ROIAlgorithms::lowPassSmooth(*vol,lbl,sg); }; break;}
    case 17:{int16_t lb=static_cast<int16_t>(m_bool.labelB->currentData().toInt());
        std::string oper=m_bool.op->currentText().toStdString();
        op=[=]{ ROIAlgorithms::booleanOp(*vol,lbl,lb,oper,lbl); }; break;}
    default: setStatus("Unknown method."); return;
    }

    // Run the segmentation on a background thread so the UI stays responsive.
    // Suspend the volume's change callback so algorithms don't fire GUI
    // updates from the worker thread; we refresh on the GUI thread on done.
    m_segRunning = true;
    setBusy(true);
    setStatus("Running…");
    vol->setNotifyEnabled(false);
    auto* thread = new QThread(this);
    auto* worker = new BgWorker(std::move(op));
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &BgWorker::run);
    connect(worker, &BgWorker::done, this, [this, thread, vol]{
        vol->setNotifyEnabled(true);
        m_segRunning = false; setBusy(false);
        setStatus("Done."); emit refreshRequested();
        thread->quit();
    });
    connect(worker, &BgWorker::failed, this, [this, thread, vol](const QString& msg){
        vol->setNotifyEnabled(true);
        m_segRunning = false; setBusy(false);
        setStatus(QString("Error: %1").arg(msg));
        QMessageBox::critical(this, "Segmentation error", msg);
        thread->quit();
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ToolPanel::setBusy(bool busy)
{
    QPushButton* btns[] = { m_applySegBtn, m_regRunBtn, m_manApplyBtn,
                            m_manResetBtn, m_quantComputeBtn, m_tacBtn };
    for (QPushButton* b : btns) if (b) b->setEnabled(!busy);
}

// ── Morph / Edit / I/O callbacks ──────────────────────────────────────────────

void ToolPanel::applyMorph(){
    if(!m_vol) return;
    int r=m_erodeDilateSlider->value();
    if(r==0){setStatus("Radius=0, no change.");return;}
    try{ ROIAlgorithms::morphErodeDilate(*m_vol,(int16_t)activeLabel(),r);
         emit refreshRequested(); setStatus("Morph applied.");
    }catch(const std::exception& e){setStatus(QString("Morph error: %1").arg(e.what()));}
}
void ToolPanel::onUndo(){ if(m_vol){m_vol->undo(); emit refreshRequested();} }
void ToolPanel::onClearLabel(){ if(m_vol){m_vol->clearLabel(activeLabel()); emit refreshRequested();} }
void ToolPanel::onClearAll()  { if(m_vol){m_vol->clearLabel(-1);            emit refreshRequested();} }

void ToolPanel::onSaveMask(){
    if(!m_vol){setStatus("No image.");return;}
    QString dir=m_saveDirEdit->text().trimmed();
    if(dir.isEmpty()){ dir=QFileDialog::getExistingDirectory(this,"Select output directory"); if(dir.isEmpty())return; m_saveDirEdit->setText(dir);}
    QString file=m_saveFileEdit->text().trimmed(); if(file.isEmpty()) file="roi_mask.nii.gz";
    QString out=dir+"/"+file;
    if(m_vol->saveMask(out.toStdString())) setStatus("Saved: "+out);
    else setStatus("Save failed.");
}
void ToolPanel::onLoadMask(){
    if(!m_vol){setStatus("No image.");return;}
    QString p=m_loadMaskEdit->text().trimmed(); if(p.isEmpty())return;
    if(m_vol->loadMask(p.toStdString())){ emit refreshRequested(); setStatus("Mask loaded.");}
    else setStatus("Load failed.");
}
void ToolPanel::setStatus(const QString& msg){ m_statusLabel->setText(msg); }

void ToolPanel::onResetWindow(){
    if(!m_vol) return;
    m_vol->resetWindow();
    QSignalBlocker b1(m_wlMinSpin), b2(m_wlMaxSpin);
    m_wlMinSpin->setValue(m_vol->vmin());
    m_wlMaxSpin->setValue(m_vol->vmax());
    emit refreshRequested();
}

void ToolPanel::onVolumeLoaded(){
    if(!m_vol) return;
    QSignalBlocker b1(m_wlMinSpin), b2(m_wlMaxSpin);
    m_wlMinSpin->setRange(m_vol->vmin()-50000, m_vol->vmax()+50000);
    m_wlMaxSpin->setRange(m_vol->vmin()-50000, m_vol->vmax()+50000);
    m_wlMinSpin->setValue(m_vol->vmin());
    m_wlMaxSpin->setValue(m_vol->vmax());
}

void ToolPanel::refreshStats(){
    if(!m_statsTable||!m_vol) return;
    auto stats=m_vol->computeAllStats();
    m_statsTable->setRowCount((int)stats.size());
    for(int i=0;i<(int)stats.size();++i){
        auto& s=stats[i];
        m_statsTable->setItem(i,0,new QTableWidgetItem(QString::number(s.label)));
        m_statsTable->setItem(i,1,new QTableWidgetItem(QString::number(s.voxelCount)));
        m_statsTable->setItem(i,2,new QTableWidgetItem(QString::number(s.volumeMm3,'f',1)));
        m_statsTable->setItem(i,3,new QTableWidgetItem(QString::number(s.meanIntensity,'f',1)));
        m_statsTable->setItem(i,4,new QTableWidgetItem(QString::number(s.stdIntensity,'f',1)));
    }
}

void ToolPanel::onSnapToCentroid(){
    if(!m_vol||!m_viewer) return;
    auto c=m_vol->labelCentroid(activeLabel());
    m_viewer->setX((int)std::round(c[0]));
    m_viewer->setY((int)std::round(c[1]));
    m_viewer->setZ((int)std::round(c[2]));
}

void ToolPanel::onPropagateLabel(int direction){
    if(!m_vol||!m_viewer) return;
    int ac=m_propAxisCombo->currentIndex();
    int axisIdx=(ac==0)?m_viewer->x():(ac==1)?m_viewer->y():m_viewer->z();
    int n=m_vol->propagateLabel(activeLabel(),ac,axisIdx,direction);
    setStatus(QString("Propagated %1 voxels.").arg(n));
    emit refreshRequested();
}

void ToolPanel::onExportCSV(){
    if(!m_vol) return;
    QString path=QFileDialog::getSaveFileName(this,"Export Statistics CSV","roisa_stats.csv","CSV (*.csv)");
    if(path.isEmpty()) return;
    if(m_vol->exportStatsCSV(path.toStdString())) setStatus("Stats exported: "+path);
    else setStatus("CSV export failed.");
}

void ToolPanel::onUpdateSurface(){
    if(!m_viewer) return;
    m_viewer->refreshSurface(activeLabel());
    setStatus("3D surface updated.");
}

void ToolPanel::onRegisterImage(){
    if(!m_vol||!m_vol->isLoaded()){ setStatus("Load a fixed image first."); return; }
    QString p=m_regMovingEdit->text().trimmed(); if(p.isEmpty()){ setStatus("No moving image path."); return; }
    setStatus("Registering… (this may take ~30 s)");
    QApplication::processEvents();
    if(m_vol->loadRegisteredImage(p.toStdString())){
        onVolumeLoaded();
        emit refreshRequested();
        setStatus("Registration complete. Display image replaced.");
    } else {
        setStatus("Registration failed. See console.");
    }
}

void ToolPanel::onResampleIsotropic()
{
    if(!m_vol||!m_vol->isLoaded()){ setStatus("No image loaded."); return; }
    float sp = (float)m_isoSpacingSpin->value();
    setStatus("Resampling… please wait");
    QApplication::processEvents();
    if(m_vol->resampleToIsotropicSpacing(sp)){
        onVolumeLoaded();
        if(m_histWidget) m_histWidget->refresh();
        if(m_viewer){
            m_viewer->setVolume(m_vol);
            m_viewer->refresh();
        }
        emit refreshRequested();
        setStatus(QString("Resampled. Display: %1×%2×%3")
                  .arg(m_vol->nx()).arg(m_vol->ny()).arg(m_vol->nz()));
    } else {
        setStatus("Resample failed — see console.");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Export — snapshot, slice series, movie
// ═══════════════════════════════════════════════════════════════════════════════

void ToolPanel::onSnapshotView(int viewIdx)
{
    if (!m_viewer) { setStatus("No viewer available."); return; }

    static const char* names[] = { "sagittal", "coronal", "axial", "3D" };
    QString path = QFileDialog::getSaveFileName(
        this,
        QString("Save %1 snapshot").arg(names[viewIdx]),
        QString("roisa_%1.png").arg(names[viewIdx]),
        "PNG (*.png);;JPEG (*.jpg);;All files (*)");
    if (path.isEmpty()) return;

    QPixmap px;
    switch (viewIdx) {
    case 0: px = m_viewer->grabSagittal(); break;
    case 1: px = m_viewer->grabCoronal();  break;
    case 2: px = m_viewer->grabAxial();    break;
    case 3: px = m_viewer->grabVtk();      break;
    default: return;
    }

    if (px.save(path)) setStatus("Saved: " + QFileInfo(path).fileName());
    else               setStatus("Snapshot save failed.");
}

void ToolPanel::onExportFrames()
{
    if (!m_vol || !m_viewer) { setStatus("No image loaded."); return; }

    QString dir = QFileDialog::getExistingDirectory(
        this, "Select output folder for frames");
    if (dir.isEmpty()) return;

    const int axis  = m_exportAxisCombo ? m_exportAxisCombo->currentIndex() : 2;
    const int total = (axis == 0) ? m_vol->nx()
                    : (axis == 1) ? m_vol->ny()
                    : m_vol->nz();

    const int origX = m_viewer->x(), origY = m_viewer->y(), origZ = m_viewer->z();
    int saved = 0;

    for (int i = 0; i < total; ++i) {
        if      (axis == 0) m_viewer->setX(i);
        else if (axis == 1) m_viewer->setY(i);
        else                m_viewer->setZ(i);
        QApplication::processEvents();

        QPixmap px;
        if      (axis == 0) px = m_viewer->grabSagittal();
        else if (axis == 1) px = m_viewer->grabCoronal();
        else                px = m_viewer->grabAxial();

        if (px.save(dir + QString("/frame_%1.png").arg(i, 4, 10, QChar('0'))))
            ++saved;

        if (i % 20 == 0)
            setStatus(QString("Exporting frame %1 / %2…").arg(i+1).arg(total));
    }

    // Restore original position
    m_viewer->setX(origX); m_viewer->setY(origY); m_viewer->setZ(origZ);
    setStatus(QString("Exported %1/%2 frames → %3").arg(saved).arg(total).arg(dir));
}

void ToolPanel::onMakeMovie()
{
    if (!m_vol || !m_viewer) { setStatus("No image loaded."); return; }

    QString outPath = QFileDialog::getSaveFileName(
        this, "Export Movie", "roisa_movie.mp4",
        "MP4 (*.mp4);;AVI (*.avi);;All files (*)");
    if (outPath.isEmpty()) return;

    const int axis  = m_exportAxisCombo ? m_exportAxisCombo->currentIndex() : 2;
    const int total = (axis == 0) ? m_vol->nx()
                    : (axis == 1) ? m_vol->ny()
                    : m_vol->nz();
    const int fps   = m_cineFpsSpin ? m_cineFpsSpin->value() : 8;

    // Write frames to temp directory
    const QString tmpDir = QDir::tempPath() + "/roisa_movie_export";
    QDir().mkpath(tmpDir);

    const int origX = m_viewer->x(), origY = m_viewer->y(), origZ = m_viewer->z();

    for (int i = 0; i < total; ++i) {
        if      (axis == 0) m_viewer->setX(i);
        else if (axis == 1) m_viewer->setY(i);
        else                m_viewer->setZ(i);
        QApplication::processEvents();

        QPixmap px;
        if      (axis == 0) px = m_viewer->grabSagittal();
        else if (axis == 1) px = m_viewer->grabCoronal();
        else                px = m_viewer->grabAxial();

        px.save(tmpDir + QString("/frame_%1.png").arg(i, 4, 10, QChar('0')));

        if (i % 20 == 0)
            setStatus(QString("Rendering frame %1 / %2…").arg(i+1).arg(total));
    }

    m_viewer->setX(origX); m_viewer->setY(origY); m_viewer->setZ(origZ);

    setStatus("Encoding with ffmpeg…");
    QApplication::processEvents();

    QProcess proc;
    proc.start("ffmpeg", {
        "-y",
        "-framerate", QString::number(fps),
        "-i",         tmpDir + "/frame_%04d.png",
        "-c:v",       "libx264",
        "-pix_fmt",   "yuv420p",
        outPath
    });
    proc.waitForFinished(120000);  // up to 2 minutes

    // Clean up temp frames
    QDir(tmpDir).removeRecursively();

    if (proc.exitCode() == 0)
        setStatus("Movie saved: " + QFileInfo(outPath).fileName());
    else
        setStatus(QString("ffmpeg failed (exit %1). Is it in PATH?")
                  .arg(proc.exitCode()));
}
