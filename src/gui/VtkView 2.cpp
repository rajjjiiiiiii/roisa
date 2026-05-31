// VtkView.cpp — VTK 3-D rendering panel (volume + label surfaces)

#include "VtkView.h"
#include "../core/ROIVolume.h"

#include <QLabel>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
// VTK implementation (compiled only when ROISA_USE_VTK is defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef ROISA_USE_VTK

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkContourFilter.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSmartVolumeMapper.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>

// ── Label colour table (16 colours, cycling) ─────────────────────────────────
static constexpr float LABEL_RGB[16][3] = {
    {1.f, .25f, .25f}, {.25f, 1.f, .25f}, {.25f, .50f, 1.f}, {1.f, 1.f, .25f},
    {1.f, .55f,  .0f}, { .8f, .25f, 1.f}, { .0f, 1.f,  1.f}, {1.f, .65f, .8f},
    {.5f, .85f,  .5f}, { .8f, .62f, .4f}, { .4f, .82f, .82f},{.82f, .4f, .6f},
    {.6f,  .4f, .82f}, { .9f, .72f, .3f}, { .3f, .72f,  .9f},{.72f, .9f, .3f},
};

static void labelRgb(int label, double& r, double& g, double& b)
{
    const auto& c = LABEL_RGB[(label - 1) % 16];
    r = c[0]; g = c[1]; b = c[2];
}

// ── VTK initialisation ────────────────────────────────────────────────────────

void VtkView::initVtk()
{
    m_renWin   = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.07, 0.07, 0.10);
    m_renWin->AddRenderer(m_renderer);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setRenderWindow(m_renWin);

    // Trackball camera (rotate / zoom / pan)
    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    m_renWin->GetInteractor()->SetInteractorStyle(style);

    // Shared transfer functions (populated in copyItkToVtk)
    m_colorTfn   = vtkSmartPointer<vtkColorTransferFunction>::New();
    m_opacityTfn = vtkSmartPointer<vtkPiecewiseFunction>::New();

    m_volProp = vtkSmartPointer<vtkVolumeProperty>::New();
    m_volProp->SetColor(m_colorTfn);
    m_volProp->SetScalarOpacity(m_opacityTfn);
    m_volProp->ShadeOn();
    m_volProp->SetInterpolationTypeToLinear();
    m_volProp->SetAmbient(0.15);
    m_volProp->SetDiffuse(0.70);
    m_volProp->SetSpecular(0.20);

    m_volMapper = vtkSmartPointer<vtkSmartVolumeMapper>::New();
    m_volMapper->SetRequestedRenderModeToGPU();

    m_volActor = vtkSmartPointer<vtkVolume>::New();
    m_volActor->SetMapper(m_volMapper);
    m_volActor->SetProperty(m_volProp);
    m_renderer->AddVolume(m_volActor);

    // Fill the widget area
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(m_vtkWidget);
}

// ── ITK display image → vtkImageData ─────────────────────────────────────────

void VtkView::copyItkToVtk()
{
    if (!m_vol || !m_vol->isLoaded()) return;

    const int    nx = m_vol->nx(), ny = m_vol->ny(), nz = m_vol->nz();
    const double sp = m_vol->voxelSpacingMm();

    m_vtkImage = vtkSmartPointer<vtkImageData>::New();
    m_vtkImage->SetDimensions(nx, ny, nz);
    m_vtkImage->SetSpacing(sp, sp, sp);
    m_vtkImage->AllocateScalars(VTK_FLOAT, 1);

    const float* src = m_vol->displayImage()->GetBufferPointer();
    float*       dst = static_cast<float*>(m_vtkImage->GetScalarPointer());
    std::memcpy(dst, src, static_cast<std::size_t>(nx) * ny * nz * sizeof(float));

    // Update transfer functions to match current W/L window
    const float vmin = m_vol->vmin(), vmax = m_vol->vmax();
    const float rng  = vmax - vmin;

    m_colorTfn->RemoveAllPoints();
    m_colorTfn->AddRGBPoint(vmin,                  0.00, 0.00, 0.00);
    m_colorTfn->AddRGBPoint(vmin + 0.20f * rng,    0.40, 0.18, 0.08);
    m_colorTfn->AddRGBPoint(vmin + 0.50f * rng,    0.72, 0.50, 0.38);
    m_colorTfn->AddRGBPoint(vmax,                  1.00, 0.95, 0.85);

    m_opacityTfn->RemoveAllPoints();
    m_opacityTfn->AddPoint(vmin,                0.000);
    m_opacityTfn->AddPoint(vmin + 0.05f * rng,  0.000);
    m_opacityTfn->AddPoint(vmin + 0.15f * rng,  0.050);
    m_opacityTfn->AddPoint(vmin + 0.40f * rng,  0.150);
    m_opacityTfn->AddPoint(vmax,                0.300);

    m_volMapper->SetInputData(m_vtkImage);
}

// ── Label surface meshes via marching cubes ───────────────────────────────────

void VtkView::rebuildSurfaces()
{
    // Clear existing surface actors from renderer
    for (auto& s : m_surfaces) m_renderer->RemoveActor(s.actor);
    m_surfaces.clear();

    if (!m_vol || !m_vol->isLoaded()) return;

    const int      nx = m_vol->nx(), ny = m_vol->ny(), nz = m_vol->nz();
    const double   sp = m_vol->voxelSpacingMm();
    const int16_t* maskBuf = m_vol->maskImage()->GetBufferPointer();

    // Discover which labels are present
    std::set<int16_t> active;
    for (int i = 0; i < nx * ny * nz; ++i)
        if (maskBuf[i] > 0) active.insert(maskBuf[i]);

    for (int16_t lbl : active) {
        if (m_surfLabel > 0 && lbl != (int16_t)m_surfLabel) continue;

        // Create binary image: this label → 255, everything else → 0
        vtkNew<vtkImageData> binImg;
        binImg->SetDimensions(nx, ny, nz);
        binImg->SetSpacing(sp, sp, sp);
        binImg->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

        auto* bin = static_cast<unsigned char*>(binImg->GetScalarPointer());
        for (int i = 0; i < nx * ny * nz; ++i)
            bin[i] = (maskBuf[i] == lbl) ? 255 : 0;

        // Marching cubes at iso = 127.5
        vtkNew<vtkContourFilter> mc;
        mc->SetInputData(binImg);
        mc->SetValue(0, 127.5);
        mc->ComputeNormalsOn();

        vtkNew<vtkPolyDataMapper> mapper;
        mapper->SetInputConnection(mc->GetOutputPort());
        mapper->ScalarVisibilityOff();

        vtkNew<vtkActor> actor;
        actor->SetMapper(mapper);

        double r, g, b;
        labelRgb(lbl, r, g, b);
        actor->GetProperty()->SetColor(r, g, b);
        actor->GetProperty()->SetOpacity(0.70);
        actor->GetProperty()->SetDiffuse(0.80);
        actor->GetProperty()->SetSpecular(0.30);
        actor->GetProperty()->SetSpecularPower(25.0);

        m_renderer->AddActor(actor);

        m_surfaces.push_back({
            (int)lbl,
            vtkSmartPointer<vtkActor>(actor.Get()),
            vtkSmartPointer<vtkPolyDataMapper>(mapper.Get())
        });
    }
}

// ── Show / hide actors according to current render mode ───────────────────────

void VtkView::applyRenderMode()
{
    const bool showVol  = (m_renderMode == 0 || m_renderMode == 2);
    const bool showSurf = (m_renderMode == 1 || m_renderMode == 2);

    if (m_volActor) m_volActor->SetVisibility(showVol  ? 1 : 0);
    for (auto& s : m_surfaces) s.actor->SetVisibility(showSurf ? 1 : 0);
}

#endif // ROISA_USE_VTK

// ─────────────────────────────────────────────────────────────────────────────
// Shared constructor / destructor / public API
// ─────────────────────────────────────────────────────────────────────────────

VtkView::VtkView(QWidget* parent) : QWidget(parent)
{
#ifdef ROISA_USE_VTK
    initVtk();
#else
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* lbl = new QLabel("3-D View\n(build with VTK to enable)", this);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet(
        "color:#666; font-size:13px; background:#0d0d0d; border:1px solid #222;");
    lay->addWidget(lbl);
#endif
}

VtkView::~VtkView() = default;

// ── isAvailable ───────────────────────────────────────────────────────────────

bool VtkView::isAvailable()
{
#ifdef ROISA_USE_VTK
    return true;
#else
    return false;
#endif
}

// ── setVolume ─────────────────────────────────────────────────────────────────

void VtkView::setVolume(ROIVolume* vol)
{
    m_vol = vol;
#ifdef ROISA_USE_VTK
    if (vol && vol->isLoaded()) {
        copyItkToVtk();
        applyRenderMode();
        m_renderer->ResetCamera();
        m_renWin->Render();
    }
#endif
}

// ── refreshVolume ─────────────────────────────────────────────────────────────

void VtkView::refreshVolume()
{
#ifdef ROISA_USE_VTK
    if (!m_vol || !m_vol->isLoaded()) return;
    copyItkToVtk();
    applyRenderMode();
    m_renWin->Render();
#endif
}

// ── refreshSurface ────────────────────────────────────────────────────────────

void VtkView::refreshSurface(int label)
{
    m_surfLabel = label;
#ifdef ROISA_USE_VTK
    if (!m_vol || !m_vol->isLoaded()) return;
    rebuildSurfaces();
    applyRenderMode();
    m_renWin->Render();
#endif
}

// ── resetCamera ───────────────────────────────────────────────────────────────

void VtkView::resetCamera()
{
#ifdef ROISA_USE_VTK
    m_renderer->ResetCamera();
    m_renWin->Render();
#endif
}

// ── setRenderMode ─────────────────────────────────────────────────────────────

void VtkView::setRenderMode(int mode)
{
    m_renderMode = mode;
#ifdef ROISA_USE_VTK
    applyRenderMode();
    m_renWin->Render();
#endif
}
