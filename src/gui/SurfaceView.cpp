// SurfaceView.cpp — 3D surface rendering via marching cubes + OpenGL

#include "SurfaceView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <cmath>
#include <array>
#include <algorithm>

// ── Marching Cubes tables (public domain – Lorensen & Cline 1987) ─────────────

static const int MC_EDGE[256] = {
    0x000,0x109,0x203,0x30a,0x406,0x50f,0x605,0x70c,
    0x80c,0x905,0xa0f,0xb06,0xc0a,0xd03,0xe09,0xf00,
    0x190,0x099,0x393,0x29a,0x596,0x49f,0x795,0x69c,
    0x99c,0x895,0xb9f,0xa96,0xd9a,0xc93,0xf99,0xe90,
    0x230,0x339,0x033,0x13a,0x636,0x73f,0x435,0x53c,
    0xa3c,0xb35,0x83f,0x936,0xe3a,0xf33,0xc39,0xd30,
    0x3a0,0x2a9,0x1a3,0x0aa,0x7a6,0x6af,0x5a5,0x4ac,
    0xbac,0xaa5,0x9af,0x8a6,0xfaa,0xea3,0xda9,0xca0,
    0x460,0x569,0x663,0x76a,0x066,0x16f,0x265,0x36c,
    0xc6c,0xd65,0xe6f,0xf66,0x86a,0x963,0xa69,0xb60,
    0x5f0,0x4f9,0x7f3,0x6fa,0x1f6,0x0ff,0x3f5,0x2fc,
    0xdfc,0xcf5,0xfff,0xef6,0x9fa,0x8f3,0xbf9,0xaf0,
    0x650,0x759,0x453,0x55a,0x256,0x35f,0x055,0x15c,
    0xe5c,0xf55,0xc5f,0xd56,0xa5a,0xb53,0x859,0x950,
    0x7c0,0x6c9,0x5c3,0x4ca,0x3c6,0x2cf,0x1c5,0x0cc,
    0xfcc,0xec5,0xdcf,0xcc6,0xbca,0xac3,0x9c9,0x8c0,
    0x8c0,0x9c9,0xac3,0xbca,0xcc6,0xdcf,0xec5,0xfcc,
    0x0cc,0x1c5,0x2cf,0x3c6,0x4ca,0x5c3,0x6c9,0x7c0,
    0x950,0x859,0xb53,0xa5a,0xd56,0xc5f,0xf55,0xe5c,
    0x15c,0x055,0x35f,0x256,0x55a,0x453,0x759,0x650,
    0xaf0,0xbf9,0x8f3,0x9fa,0xef6,0xfff,0xcf5,0xdfc,
    0x2fc,0x3f5,0x0ff,0x1f6,0x6fa,0x7f3,0x4f9,0x5f0,
    0xb60,0xa69,0x963,0x86a,0xf66,0xe6f,0xd65,0xc6c,
    0x36c,0x265,0x16f,0x066,0x76a,0x663,0x569,0x460,
    0xca0,0xda9,0xea3,0xfaa,0x8a6,0x9af,0xaa5,0xbac,
    0x4ac,0x5a5,0x6af,0x7a6,0x0aa,0x1a3,0x2a9,0x3a0,
    0xd30,0xc39,0xf33,0xe3a,0x936,0x83f,0xb35,0xa3c,
    0x53c,0x435,0x73f,0x636,0x13a,0x033,0x339,0x230,
    0xe90,0xf99,0xc93,0xd9a,0xa96,0xb9f,0x895,0x99c,
    0x69c,0x795,0x49f,0x596,0x29a,0x393,0x099,0x190,
    0xf00,0xe09,0xd03,0xc0a,0xb06,0xa0f,0x905,0x80c,
    0x70c,0x605,0x50f,0x406,0x30a,0x203,0x109,0x000
};

static const int MC_TRIS[256][16] = {
    {-1},{0,8,3,-1},{0,1,9,-1},{1,8,3,9,8,1,-1},{1,2,10,-1},{0,8,3,1,2,10,-1},{9,2,10,0,2,9,-1},{2,8,3,2,10,8,10,9,8,-1},
    {3,11,2,-1},{0,11,2,8,11,0,-1},{1,9,0,2,3,11,-1},{1,11,2,1,9,11,9,8,11,-1},{3,10,1,11,10,3,-1},{0,10,1,0,8,10,8,11,10,-1},{3,9,0,3,11,9,11,10,9,-1},{9,8,10,10,8,11,-1},
    {4,7,8,-1},{4,3,0,7,3,4,-1},{0,1,9,8,4,7,-1},{4,1,9,4,7,1,7,3,1,-1},{8,4,7,3,11,2,1,2,10,-1},{11,4,7,2,11,10,2,4,1,-1},{9,2,10,0,2,9,8,4,7,-1},{2,10,9,2,9,7,2,7,11,7,9,4,-1},
    {8,4,7,3,11,2,-1},{11,4,7,11,2,4,2,0,4,-1},{9,0,1,8,4,7,2,3,11,-1},{4,7,11,9,4,11,9,11,2,9,2,1,-1},{3,10,1,3,11,10,7,8,4,-1},{1,11,10,1,4,11,1,0,4,7,11,4,-1},{4,7,8,9,0,11,9,11,10,11,0,3,-1},{4,7,11,4,11,9,9,11,10,-1},
    {9,5,4,-1},{9,5,4,0,8,3,-1},{0,5,4,1,5,0,-1},{8,5,4,8,3,5,3,1,5,-1},{1,2,10,9,5,4,-1},{3,0,8,1,2,10,4,9,5,-1},{5,2,10,5,4,2,4,0,2,-1},{2,10,5,3,2,5,3,5,4,3,4,8,-1},
    {9,5,4,2,3,11,-1},{0,11,2,0,8,11,4,9,5,-1},{0,5,4,1,5,0,2,3,11,-1},{2,1,5,2,5,8,2,8,11,4,8,5,-1},{10,3,11,10,1,3,9,5,4,-1},{4,9,5,0,8,1,8,10,1,8,11,10,-1},{5,4,0,5,0,11,5,11,10,11,0,3,-1},{5,4,8,5,8,10,10,8,11,-1},
    {9,7,8,5,7,9,-1},{9,3,0,9,5,3,5,7,3,-1},{0,7,8,0,1,7,1,5,7,-1},{1,5,3,3,5,7,-1},{9,7,8,9,5,7,10,1,2,-1},{10,1,2,9,5,0,5,3,0,5,7,3,-1},{8,0,2,8,2,5,8,5,7,10,5,2,-1},{2,10,5,2,5,3,3,5,7,-1},
    {7,9,5,7,8,9,3,11,2,-1},{9,5,7,9,7,2,9,2,0,2,7,11,-1},{2,3,11,0,1,8,1,7,8,1,5,7,-1},{11,2,1,11,1,7,7,1,5,-1},{9,5,8,8,5,7,10,1,3,10,3,11,-1},{5,7,0,5,0,9,7,11,0,1,0,10,11,10,0,-1},{11,10,0,11,0,3,10,5,0,8,0,7,5,7,0,-1},{11,10,5,7,11,5,-1},
    {10,6,5,-1},{0,8,3,5,10,6,-1},{9,0,1,5,10,6,-1},{1,8,3,1,9,8,5,10,6,-1},{1,6,5,2,6,1,-1},{1,6,5,1,2,6,3,0,8,-1},{9,6,5,9,0,6,0,2,6,-1},{5,9,8,5,8,2,5,2,6,3,2,8,-1},
    {2,3,11,10,6,5,-1},{11,0,8,11,2,0,10,6,5,-1},{0,1,9,2,3,11,5,10,6,-1},{5,10,6,1,9,2,9,11,2,9,8,11,-1},{6,3,11,6,5,3,5,1,3,-1},{0,8,11,0,11,5,0,5,1,5,11,6,-1},{3,11,6,0,3,6,0,6,5,0,5,9,-1},{6,5,9,6,9,11,11,9,8,-1},
    {5,10,6,4,7,8,-1},{4,3,0,4,7,3,6,5,10,-1},{1,9,0,5,10,6,8,4,7,-1},{10,6,5,1,9,7,1,7,3,7,9,4,-1},{6,1,2,6,5,1,4,7,8,-1},{1,2,5,5,2,6,3,0,4,3,4,7,-1},{8,4,7,9,0,5,0,6,5,0,2,6,-1},{7,3,9,7,9,4,3,2,9,5,9,6,2,6,9,-1},
    {3,11,2,7,8,4,10,6,5,-1},{5,10,6,4,7,2,4,2,0,2,7,11,-1},{0,1,9,4,7,8,2,3,11,5,10,6,-1},{9,2,1,9,11,2,9,4,11,7,11,4,5,10,6,-1},{8,4,7,3,11,5,3,5,1,5,11,6,-1},{5,1,11,5,11,6,1,0,11,7,11,4,0,4,11,-1},{0,5,9,0,6,5,0,3,6,11,6,3,8,4,7,-1},{6,5,9,6,9,11,4,7,9,7,11,9,-1},
    {10,4,9,6,4,10,-1},{4,10,6,4,9,10,0,8,3,-1},{10,0,1,10,6,0,6,4,0,-1},{8,3,1,8,1,6,8,6,4,6,1,10,-1},{1,4,9,1,2,4,2,6,4,-1},{3,0,8,1,2,9,2,4,9,2,6,4,-1},{0,2,4,4,2,6,-1},{8,3,2,8,2,4,4,2,6,-1},
    {10,4,9,10,6,4,11,2,3,-1},{0,8,2,2,8,11,4,9,10,4,10,6,-1},{3,11,2,0,1,6,0,6,4,6,1,10,-1},{6,4,1,6,1,10,4,8,1,2,1,11,8,11,1,-1},{9,6,4,9,3,6,9,1,3,11,6,3,-1},{8,11,1,8,1,0,11,6,1,9,1,4,6,4,1,-1},{3,11,6,3,6,0,0,6,4,-1},{6,4,8,11,6,8,-1},
    {7,10,6,7,8,10,8,9,10,-1},{0,7,3,0,10,7,0,9,10,6,7,10,-1},{10,6,7,1,10,7,1,7,8,1,8,0,-1},{10,6,7,10,7,1,1,7,3,-1},{1,2,6,1,6,8,1,8,9,8,6,7,-1},{2,6,9,2,9,1,6,7,9,0,9,3,7,3,9,-1},{7,8,0,7,0,6,6,0,2,-1},{7,3,2,6,7,2,-1},
    {2,3,11,10,6,8,10,8,9,8,6,7,-1},{2,0,7,2,7,11,0,9,7,6,7,10,9,10,7,-1},{1,8,0,1,7,8,1,10,7,6,7,10,2,3,11,-1},{11,2,1,11,1,7,10,6,1,6,7,1,-1},{8,9,6,8,6,7,9,1,6,11,6,3,1,3,6,-1},{0,9,1,11,6,7,-1},{7,8,0,7,0,6,3,11,0,11,6,0,-1},{7,11,6,-1},
    {7,6,11,-1},{3,0,8,11,7,6,-1},{0,1,9,11,7,6,-1},{8,1,9,8,3,1,11,7,6,-1},{10,1,2,6,11,7,-1},{1,2,10,3,0,8,6,11,7,-1},{2,9,0,2,10,9,6,11,7,-1},{6,11,7,2,10,3,10,8,3,10,9,8,-1},
    {7,2,3,6,2,7,-1},{7,0,8,7,6,0,6,2,0,-1},{2,7,6,2,3,7,0,1,9,-1},{1,6,2,1,8,6,1,9,8,8,7,6,-1},{10,7,6,10,1,7,1,3,7,-1},{10,7,6,1,7,10,1,8,7,1,0,8,-1},{0,3,7,0,7,10,0,10,9,6,10,7,-1},{7,6,10,7,10,8,8,10,9,-1},
    {6,8,4,11,8,6,-1},{3,6,11,3,0,6,0,4,6,-1},{8,6,11,8,4,6,9,0,1,-1},{9,4,6,9,6,3,9,3,1,11,3,6,-1},{6,8,4,6,11,8,2,10,1,-1},{1,2,10,3,0,11,0,6,11,0,4,6,-1},{4,11,8,4,6,11,0,2,9,2,10,9,-1},{10,9,3,10,3,2,9,4,3,11,3,6,4,6,3,-1},
    {8,2,3,8,4,2,4,6,2,-1},{0,4,2,4,6,2,-1},{1,9,0,2,3,4,2,4,6,4,3,8,-1},{1,9,4,1,4,2,2,4,6,-1},{8,1,3,8,6,1,8,4,6,6,10,1,-1},{10,1,0,10,0,6,6,0,4,-1},{4,6,3,4,3,8,6,10,3,0,3,9,10,9,3,-1},{10,9,4,6,10,4,-1},
    {4,9,5,7,6,11,-1},{0,8,3,4,9,5,11,7,6,-1},{5,0,1,5,4,0,7,6,11,-1},{11,7,6,8,3,4,3,5,4,3,1,5,-1},{9,5,4,10,1,2,7,6,11,-1},{6,11,7,1,2,10,0,8,3,4,9,5,-1},{7,6,11,5,4,10,4,2,10,4,0,2,-1},{3,4,8,3,5,4,3,2,5,10,5,2,11,7,6,-1},
    {7,2,3,7,6,2,5,4,9,-1},{9,5,4,0,8,6,0,6,2,6,8,7,-1},{3,6,2,3,7,6,1,5,0,5,4,0,-1},{6,2,8,6,8,7,2,1,8,4,8,5,1,5,8,-1},{9,5,4,10,1,6,1,7,6,1,3,7,-1},{1,6,10,1,7,6,1,0,7,8,7,0,9,5,4,-1},{4,0,10,4,10,5,0,3,10,6,10,7,3,7,10,-1},{7,6,10,7,10,8,5,4,10,4,8,10,-1},
    {6,9,5,6,11,9,11,8,9,-1},{3,6,11,0,6,3,0,5,6,0,9,5,-1},{0,11,8,0,5,11,0,1,5,5,6,11,-1},{6,11,3,6,3,5,5,3,1,-1},{1,2,10,9,5,11,9,11,8,11,5,6,-1},{0,11,3,0,6,11,0,9,6,5,6,9,1,2,10,-1},{11,8,5,11,5,6,8,0,5,10,5,2,0,2,5,-1},{6,11,3,6,3,5,2,10,3,10,5,3,-1},
    {5,8,9,5,2,8,5,6,2,3,8,2,-1},{9,5,6,9,6,0,0,6,2,-1},{1,5,8,1,8,0,5,6,8,3,8,2,6,2,8,-1},{1,5,6,2,1,6,-1},{1,3,6,1,6,10,3,8,6,5,6,9,8,9,6,-1},{10,1,0,10,0,6,9,5,0,5,6,0,-1},{0,3,8,5,6,10,-1},{10,5,6,-1},
    {11,5,10,7,5,11,-1},{11,5,10,11,7,5,8,3,0,-1},{5,11,7,5,10,11,1,9,0,-1},{10,7,5,10,11,7,9,8,1,8,3,1,-1},{11,1,2,11,7,1,7,5,1,-1},{0,8,3,1,2,7,1,7,5,7,2,11,-1},{9,7,5,9,2,7,9,0,2,2,11,7,-1},{7,5,2,7,2,11,5,9,2,3,2,8,9,8,2,-1},
    {2,5,10,2,3,5,3,7,5,-1},{8,2,0,8,5,2,8,7,5,10,2,5,-1},{9,0,1,2,3,5,2,5,10,5,3,7,-1},{8,2,9,8,9,7,2,10,9,5,9,3,10,3,9,-1},{1,7,5,1,3,7,-1},{1,0,8,1,8,5,5,8,7,-1},{9,0,3,9,3,5,5,3,7,-1},{9,8,7,5,9,7,-1},
    {5,8,4,5,10,8,10,11,8,-1},{5,0,4,5,11,0,5,10,11,11,3,0,-1},{0,1,9,8,4,10,8,10,11,10,4,5,-1},{10,11,4,10,4,5,11,3,4,9,4,1,3,1,4,-1},{2,5,1,2,8,5,2,11,8,4,5,8,-1},{0,4,11,0,11,3,4,5,11,2,11,1,5,1,11,-1},{0,2,5,0,5,9,2,11,5,4,5,8,11,8,5,-1},{9,4,5,2,11,3,-1},
    {2,5,10,3,5,2,3,4,5,3,8,4,-1},{5,10,2,5,2,4,4,2,0,-1},{3,10,2,3,5,10,3,8,5,4,5,8,0,1,9,-1},{5,10,2,5,2,4,1,9,2,9,4,2,-1},{8,4,5,8,5,3,3,5,1,-1},{0,4,5,1,0,5,-1},{8,4,5,8,5,3,9,0,5,0,3,5,-1},{9,4,5,-1},
    {4,11,7,4,9,11,9,10,11,-1},{0,8,3,4,9,7,9,11,7,9,10,11,-1},{1,10,11,1,11,4,1,4,0,7,4,11,-1},{3,1,4,3,4,8,1,10,4,7,4,11,10,11,4,-1},{4,11,7,9,11,4,9,2,11,9,1,2,-1},{9,7,4,9,11,7,9,1,11,2,11,1,0,8,3,-1},{11,7,4,11,4,2,2,4,0,-1},{11,7,4,11,4,2,8,3,4,3,2,4,-1},
    {2,9,10,2,7,9,2,3,7,7,4,9,-1},{9,10,7,9,7,4,10,2,7,8,7,0,2,0,7,-1},{3,7,10,3,10,2,7,4,10,1,10,0,4,0,10,-1},{1,10,2,8,7,4,-1},{4,9,1,4,1,7,7,1,3,-1},{4,9,1,4,1,7,0,8,1,8,7,1,-1},{4,0,3,7,4,3,-1},{4,8,7,-1},
    {9,10,8,10,11,8,-1},{3,0,9,3,9,11,11,9,10,-1},{0,1,10,0,10,8,8,10,11,-1},{3,1,10,11,3,10,-1},{1,2,11,1,11,9,9,11,8,-1},{3,0,9,3,9,11,1,2,9,2,11,9,-1},{0,2,11,8,0,11,-1},{3,2,11,-1},
    {2,3,8,2,8,10,10,8,9,-1},{9,10,2,0,9,2,-1},{2,3,8,2,8,10,0,1,8,1,10,8,-1},{1,10,2,-1},{1,3,8,9,1,8,-1},{0,9,1,-1},{0,3,8,-1},{-1}
};

// ── Interpolation helper ──────────────────────────────────────────────────────

static float lerp(float a, float b, float va, float vb, float iso)
{
    float d = vb - va;
    return std::abs(d) < 1e-6f ? 0.5f : (iso - va) / d * (b - a) + a;
}

// ── SurfaceView implementation ────────────────────────────────────────────────

SurfaceView::SurfaceView(QWidget* parent) : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    setFormat(fmt);
    setMinimumSize(200, 200);
}

SurfaceView::~SurfaceView()
{
    makeCurrent();
    m_vao.destroy();
    m_vbo.destroy();
    doneCurrent();
}

void SurfaceView::setVolume(ROIVolume* vol) { m_vol = vol; }
void SurfaceView::setLabel(int label)       { m_label = label; }

void SurfaceView::refresh()
{
    if (!isValid()) return;
    makeCurrent();
    buildMesh();
    doneCurrent();
    update();
}

// ── Mesh extraction ───────────────────────────────────────────────────────────

void SurfaceView::marchCube(const float corners[8], float iso,
                              float cx, float cy, float cz,
                              float dx, float dy, float dz,
                              std::vector<float>& verts)
{
    // Corner positions relative to cube origin
    static const float PX[8] = {0,1,1,0,0,1,1,0};
    static const float PY[8] = {0,0,1,1,0,0,1,1};
    static const float PZ[8] = {0,0,0,0,1,1,1,1};
    // Edge endpoints
    static const int EA[12]  = {0,1,2,3,4,5,6,7,0,1,2,3};
    static const int EB[12]  = {1,2,3,0,5,6,7,4,4,5,6,7};

    int idx = 0;
    for (int i=0;i<8;++i) if (corners[i] >= iso) idx |= (1<<i);

    int edgeMask = MC_EDGE[idx];
    if (!edgeMask) return;

    // Interpolated edge vertex positions
    float ev[12][3];
    for (int i=0;i<12;++i) {
        if (edgeMask & (1<<i)) {
            int a = EA[i], b = EB[i];
            ev[i][0] = cx + lerp(PX[a],PX[b],corners[a],corners[b],iso)*dx;
            ev[i][1] = cy + lerp(PY[a],PY[b],corners[a],corners[b],iso)*dy;
            ev[i][2] = cz + lerp(PZ[a],PZ[b],corners[a],corners[b],iso)*dz;
        }
    }

    const int* tbl = MC_TRIS[idx];
    for (int i=0; tbl[i] != -1; i+=3) {
        // Triangle vertices
        float v0[3], v1[3], v2[3];
        for (int k=0;k<3;++k) { v0[k]=ev[tbl[i  ]][k]; v1[k]=ev[tbl[i+1]][k]; v2[k]=ev[tbl[i+2]][k]; }
        // Flat normal from cross product
        float ax=v1[0]-v0[0], ay=v1[1]-v0[1], az=v1[2]-v0[2];
        float bx=v2[0]-v0[0], by=v2[1]-v0[1], bz=v2[2]-v0[2];
        float nx=ay*bz-az*by, ny=az*bx-ax*bz, nz=ax*by-ay*bx;
        float len=std::sqrt(nx*nx+ny*ny+nz*nz);
        if (len > 1e-8f) { nx/=len; ny/=len; nz/=len; }
        // Push 3 vertices with shared normal (interleaved pos+norm)
        for (int vi=0;vi<3;++vi) {
            const float* v = (vi==0?v0:vi==1?v1:v2);
            verts.push_back(v[0]); verts.push_back(v[1]); verts.push_back(v[2]);
            verts.push_back(nx);   verts.push_back(ny);   verts.push_back(nz);
        }
    }
}

void SurfaceView::buildMesh()
{
    if (!m_vol || !m_vol->isLoaded()) { m_triCount = 0; return; }

    int NX = m_vol->nx(), NY = m_vol->ny(), NZ = m_vol->nz();
    const int16_t* mbuf = m_vol->maskImage()->GetBufferPointer();

    // Build binary volume: 1.0 where label matches
    std::vector<float> field(NX*NY*NZ, 0.f);
    for (int z=0;z<NZ;++z)
      for (int y=0;y<NY;++y)
        for (int x=0;x<NX;++x) {
            int16_t v = mbuf[x + NX*y + NX*NY*z];
            if (m_label <= 0 ? v != 0 : v == m_label)
                field[x + NX*y + NX*NY*z] = 1.f;
        }

    // Normalise cube size so longest axis = 2
    float sc = 2.f / std::max({NX, NY, NZ});
    float ox = -NX*sc*0.5f, oy = -NY*sc*0.5f, oz = -NZ*sc*0.5f;

    std::vector<float> verts;
    verts.reserve(1<<18);

    for (int z=0;z<NZ-1;++z)
      for (int y=0;y<NY-1;++y)
        for (int x=0;x<NX-1;++x) {
            float corners[8];
            corners[0]=field[x   +NX*y    +NX*NY*z  ];
            corners[1]=field[x+1 +NX*y    +NX*NY*z  ];
            corners[2]=field[x+1 +NX*(y+1)+NX*NY*z  ];
            corners[3]=field[x   +NX*(y+1)+NX*NY*z  ];
            corners[4]=field[x   +NX*y    +NX*NY*(z+1)];
            corners[5]=field[x+1 +NX*y    +NX*NY*(z+1)];
            corners[6]=field[x+1 +NX*(y+1)+NX*NY*(z+1)];
            corners[7]=field[x   +NX*(y+1)+NX*NY*(z+1)];
            marchCube(corners, 0.5f,
                      ox+x*sc, oy+y*sc, oz+z*sc,
                      sc, sc, sc, verts);
        }

    m_triCount = (int)(verts.size() / 6 / 3);
    if (m_triCount == 0) return;

    m_vbo.bind();
    m_vbo.allocate(verts.data(), (int)(verts.size()*sizeof(float)));
    m_vao.bind();
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6*sizeof(float));
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, 3*sizeof(float), 3, 6*sizeof(float));
    m_vao.release();
    m_vbo.release();
}

// ── OpenGL ────────────────────────────────────────────────────────────────────

void SurfaceView::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.08f, 0.08f, 0.08f, 1.f);
    glEnable(GL_DEPTH_TEST);

    const char* vsSrc = R"(
        #version 330 core
        layout(location=0) in vec3 pos;
        layout(location=1) in vec3 norm;
        out vec3 vNorm;
        out vec3 vPos;
        uniform mat4 mvp;
        uniform mat3 normalMat;
        void main(){
            vPos  = pos;
            vNorm = normalize(normalMat * norm);
            gl_Position = mvp * vec4(pos,1.0);
        }
    )";
    const char* fsSrc = R"(
        #version 330 core
        in  vec3 vNorm;
        in  vec3 vPos;
        out vec4 fragColor;
        uniform vec3 labelColor;
        void main(){
            vec3 L = normalize(vec3(1,2,3));
            float diff  = max(dot(vNorm,L), 0.0);
            float amb   = 0.25;
            vec3 V = normalize(-vPos);
            vec3 R = reflect(-L, vNorm);
            float spec  = pow(max(dot(R,V),0.0), 32.0) * 0.4;
            vec3 col = labelColor * (amb + diff) + vec3(spec);
            fragColor = vec4(col, 1.0);
        }
    )";

    m_program.addShaderFromSourceCode(QOpenGLShader::Vertex,   vsSrc);
    m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fsSrc);
    m_program.link();

    m_vao.create();
    m_vbo.create();
    m_vao.bind();
    m_vbo.bind();
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 3, 6*sizeof(float));
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, 3*sizeof(float), 3, 6*sizeof(float));
    m_vao.release();
    m_vbo.release();
}

void SurfaceView::resizeGL(int w, int h)
{
    m_proj.setToIdentity();
    m_proj.perspective(45.f, float(w)/std::max(1,h), 0.01f, 100.f);
}

void SurfaceView::paintEvent(QPaintEvent* e)
{
    // Let QOpenGLWidget do its GL compositing first
    QOpenGLWidget::paintEvent(e);

    // Overlay placeholder text using QPainter *after* GL is done — this avoids
    // the compositor-bleed artefact that occurs when QPainter is called inside paintGL()
    if (m_triCount == 0) {
        QPainter p(this);
        p.fillRect(rect(), QColor(18, 18, 18));   // solid dark background
        p.setPen(QColor(100, 100, 100));
        QFont f = p.font();
        f.setPointSize(9);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   "3D Surface\n\nApply a label, then click\n\"Update 3D Surface\"");
    }
}

void SurfaceView::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m_triCount == 0) return;   // placeholder drawn in paintEvent instead

    QMatrix4x4 view, model;
    view.translate(0, 0, -m_distance);
    model.rotate(m_pitch, 1, 0, 0);
    model.rotate(m_yaw,   0, 1, 0);
    QMatrix4x4 mvp = m_proj * view * model;
    QMatrix3x3 normalMat = model.normalMatrix();

    // Label colour (matches tab20[0] as default)
    float lc[3] = {0.12f, 0.47f, 0.71f};
    if (m_label >= 1 && m_label <= 20) {
        extern const uint8_t TAB20[20][3];  // forward-decl won't work, use inline
        // Hardcode a palette shortcut:
        static const float COLS[20][3] = {
            {0.12f,0.47f,0.71f},{0.68f,0.78f,0.91f},{1.00f,0.50f,0.05f},{1.00f,0.73f,0.47f},
            {0.17f,0.63f,0.17f},{0.60f,0.87f,0.54f},{0.84f,0.15f,0.16f},{1.00f,0.60f,0.59f},
            {0.58f,0.40f,0.74f},{0.77f,0.69f,0.83f},{0.55f,0.34f,0.29f},{0.77f,0.61f,0.58f},
            {0.89f,0.47f,0.76f},{0.97f,0.71f,0.82f},{0.50f,0.50f,0.50f},{0.78f,0.78f,0.78f},
            {0.74f,0.74f,0.13f},{0.86f,0.86f,0.55f},{0.09f,0.75f,0.81f},{0.62f,0.85f,0.90f}
        };
        lc[0]=COLS[m_label-1][0]; lc[1]=COLS[m_label-1][1]; lc[2]=COLS[m_label-1][2];
    }

    m_program.bind();
    m_program.setUniformValue("mvp",        mvp);
    m_program.setUniformValue("normalMat",  normalMat);
    m_program.setUniformValue("labelColor", lc[0], lc[1], lc[2]);

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, m_triCount * 3);
    m_vao.release();
    m_program.release();
}

void SurfaceView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_mouseDown = true;
        m_lastMouse = e->pos();
    }
}

void SurfaceView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        m_mouseDown = false;
}

void SurfaceView::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_mouseDown) return;
    QPoint d = e->pos() - m_lastMouse;
    m_yaw   += d.x() * 0.5f;
    m_pitch += d.y() * 0.5f;
    m_pitch  = std::max(-89.f, std::min(89.f, m_pitch));
    m_lastMouse = e->pos();
    update();
}

void SurfaceView::wheelEvent(QWheelEvent* e)
{
    m_distance = std::max(0.5f, m_distance - e->angleDelta().y()*0.005f);
    update();
    e->accept();
}
