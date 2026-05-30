// ToolPanel.cpp — Control panel implementation

#include "ToolPanel.h"
#include "OrthoViewer.h"
#include "../core/ROIAlgorithms.h"
#include "../core/ROIVolume.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

// ── Small helpers ─────────────────────────────────────────────────────────────

static QDoubleSpinBox* dbl(double lo,double hi,double val,double step,int dec=2){
    auto* s=new QDoubleSpinBox; s->setRange(lo,hi); s->setValue(val);
    s->setSingleStep(step); s->setDecimals(dec); return s;
}
static QSpinBox* intSpin(int lo,int hi,int val){
    auto* s=new QSpinBox; s->setRange(lo,hi); s->setValue(val); return s;
}

// ── Constructor ───────────────────────────────────────────────────────────────

ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent)
{
    setMinimumWidth(280); setMaximumWidth(340);
    auto* ml = new QVBoxLayout(this);
    ml->setContentsMargins(4,4,4,4); ml->setSpacing(4);
    ml->addWidget(buildToolGroup());
    ml->addWidget(buildNavGroup());
    ml->addWidget(buildSegGroup());
    ml->addWidget(buildMorphGroup());
    ml->addWidget(buildEditGroup());
    ml->addWidget(buildIOGroup());
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color:#aaa;font-size:10px;");
    ml->addWidget(m_statusLabel);
    ml->addStretch();
}

// ── Tool group ────────────────────────────────────────────────────────────────

QGroupBox* ToolPanel::buildToolGroup()
{
    auto* gb=new QGroupBox("Tool & Label"); auto* l=new QVBoxLayout(gb);
    m_toolCombo=new QComboBox; m_toolCombo->addItems({"Paint","Erase","Segment"});
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

// ── Public API ────────────────────────────────────────────────────────────────

void ToolPanel::setVolume(ROIVolume* vol)
{
    m_vol=vol;
    if(!vol||!vol->isLoaded()) return;
    {QSignalBlocker b(m_xSlider); m_xSlider->setMaximum(vol->nx()-1); m_xSlider->setValue(vol->nx()/2); m_xLabel->setText(QString("X %1").arg(vol->nx()/2));}
    {QSignalBlocker b(m_ySlider); m_ySlider->setMaximum(vol->ny()-1); m_ySlider->setValue(vol->ny()/2); m_yLabel->setText(QString("Y %1").arg(vol->ny()/2));}
    {QSignalBlocker b(m_zSlider); m_zSlider->setMaximum(vol->nz()-1); m_zSlider->setValue(vol->nz()/2); m_zLabel->setText(QString("Z %1").arg(vol->nz()/2));}
    float vmin=vol->vmin(), vmax=vol->vmax();
    m_thresh.lower->setRange(vmin,vmax); m_thresh.lower->setValue(vmin);
    m_thresh.upper->setRange(vmin,vmax); m_thresh.upper->setValue(vmax);
    m_nbr.lower->setRange(vmin,vmax);    m_nbr.lower->setValue(vmin);
    m_nbr.upper->setRange(vmin,vmax);    m_nbr.upper->setValue(vmax);
}
void ToolPanel::setViewer(OrthoViewer* v){ m_viewer=v; }
int     ToolPanel::activeLabel() const { return m_labelCombo->currentData().toInt(); }
QString ToolPanel::toolMode()    const { return m_toolCombo->currentText().toLower(); }
int     ToolPanel::brushRadius() const { return m_brushRadiusSpin ? m_brushRadiusSpin->value() : 3; }
int     ToolPanel::brushShape()  const { return m_brushShapeCombo ? m_brushShapeCombo->currentIndex() : 0; }
bool    ToolPanel::twoDOnly()    const { return m_twoDCheckbox    ? m_twoDCheckbox->isChecked() : false; }

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

void ToolPanel::onNavXChanged(int x){ m_xLabel->setText(QString("X %1").arg(x)); if(m_viewer) m_viewer->setX(x); }
void ToolPanel::onNavYChanged(int y){ m_yLabel->setText(QString("Y %1").arg(y)); if(m_viewer) m_viewer->setY(y); }
void ToolPanel::onNavZChanged(int z){ m_zLabel->setText(QString("Z %1").arg(z)); if(m_viewer) m_viewer->setZ(z); }
void ToolPanel::onSegMethodChanged(int idx){ m_segParamStack->setCurrentIndex(idx); }

// ── Apply segmentation ────────────────────────────────────────────────────────

void ToolPanel::applySegmentation()
{
    if(!m_vol||!m_vol->isLoaded()){ setStatus("No image loaded."); return; }
    static const int SEED_METHODS[]={1,2,3,4,5,6,11};
    int method=m_segMethodCombo->currentData().toInt();
    for(int sm:SEED_METHODS) if(sm==method&&!m_seedSet){
        setStatus("Switch to Segment tool and click on image to set a seed first."); return; }
    int16_t lbl=static_cast<int16_t>(activeLabel());
    try {
        switch(method){
        case 0:{int axis=-1,si=-1;
            if(m_thresh.sliceOnly->isChecked()){
                int ac=m_thresh.sliceAxis->currentIndex();
                axis=(ac==0)?2:(ac==1)?1:0;
                if(m_viewer) si=(axis==2)?m_viewer->z():(axis==1)?m_viewer->y():m_viewer->x();
            }
            ROIAlgorithms::thresholdSegment(*m_vol,(float)m_thresh.lower->value(),(float)m_thresh.upper->value(),lbl,axis,si); break;}
        case 1: ROIAlgorithms::regionGrow(*m_vol,m_seedX,m_seedY,m_seedZ,(float)m_grow.tolerance->value(),lbl); break;
        case 2: ROIAlgorithms::connectedThreshold(*m_vol,m_seedX,m_seedY,m_seedZ,(float)m_thresh.lower->value(),(float)m_thresh.upper->value(),lbl); break;
        case 3: ROIAlgorithms::neighborhoodConnected(*m_vol,m_seedX,m_seedY,m_seedZ,(float)m_nbr.lower->value(),(float)m_nbr.upper->value(),m_nbr.radius->value(),lbl); break;
        case 4: ROIAlgorithms::confidenceConnected(*m_vol,m_seedX,m_seedY,m_seedZ,(float)m_conf.multiplier->value(),m_conf.iterations->value(),m_conf.radius->value(),lbl); break;
        case 5:{int ac=m_ff2d.axis->currentIndex(); int ax=(ac==0)?2:(ac==1)?1:0;
            ROIAlgorithms::floodFill2D(*m_vol,m_seedX,m_seedY,m_seedZ,ax,(float)m_ff2d.tolerance->value(),lbl); break;}
        case 6: ROIAlgorithms::fastMarching(*m_vol,m_seedX,m_seedY,m_seedZ,(float)m_fm.stoppingValue->value(),lbl); break;
        case 7: ROIAlgorithms::otsuThreshold(*m_vol,lbl,m_otsu.bins->value(),m_otsu.classes->value()); break;
        case 8: ROIAlgorithms::kMeansCluster(*m_vol,m_kmeans.k->value(),lbl); break;
        case 9: ROIAlgorithms::levelSetRefine(*m_vol,lbl,m_ls.iterations->value(),(float)m_ls.propagation->value(),(float)m_ls.curvature->value()); break;
        case 10: ROIAlgorithms::watershed(*m_vol,lbl); break;
        case 11: ROIAlgorithms::roiConnected(*m_vol,m_seedX,m_seedY,m_seedZ,lbl,lbl); break;
        case 12: ROIAlgorithms::removeSmallComponents(*m_vol,lbl,m_minSize.minSize->value()); break;
        case 13: ROIAlgorithms::connectedComponents(*m_vol,lbl,m_cc.maxComp->value()); break;
        case 14:{int ac=m_fill.axis->currentIndex(); int ax=(ac==0)?-1:(ac==1)?2:(ac==2)?1:0;
            ROIAlgorithms::fillHoles(*m_vol,lbl,ax); break;}
        case 15: ROIAlgorithms::makeShell(*m_vol,lbl,m_shell.thickness->value()); break;
        case 16: ROIAlgorithms::lowPassSmooth(*m_vol,lbl,(float)m_smooth.sigma->value()); break;
        case 17:{int16_t lb=static_cast<int16_t>(m_bool.labelB->currentData().toInt());
            ROIAlgorithms::booleanOp(*m_vol,lbl,lb,m_bool.op->currentText().toStdString(),lbl); break;}
        default: setStatus("Unknown method."); return;
        }
        setStatus("Done."); emit refreshRequested();
    } catch(const std::exception& e){
        setStatus(QString("Error: %1").arg(e.what()));
        QMessageBox::critical(this,"Segmentation error",e.what());
    }
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
