#pragma once
// SurfaceView.h — 3D OpenGL surface preview using marching cubes

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QPoint>
#include <vector>
#include "../core/ROIVolume.h"

class SurfaceView : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit SurfaceView(QWidget* parent = nullptr);
    ~SurfaceView();

    void setVolume(ROIVolume* vol);
    void setLabel(int label);
    void refresh();   // re-extract mesh from current vol/label

    bool hasMesh() const { return m_triCount > 0; }

protected:
    void initializeGL()                override;
    void resizeGL(int w, int h)        override;
    void paintGL()                     override;
    // Paint placeholder text *after* GL when no mesh (avoids compositor bleed)
    void paintEvent(QPaintEvent* e)    override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*)  override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*)      override;

private:
    ROIVolume*  m_vol{nullptr};
    int         m_label{1};

    // OpenGL objects
    QOpenGLShaderProgram  m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer         m_vbo{QOpenGLBuffer::VertexBuffer};
    int                   m_triCount{0};

    // Camera
    float    m_yaw{30.f}, m_pitch{20.f}, m_distance{3.5f};
    QPoint   m_lastMouse;
    bool     m_mouseDown{false};
    QMatrix4x4 m_proj;

    void buildMesh();
    static void marchCube(const float corners[8],
                          float iso,
                          float cx, float cy, float cz,
                          float dx, float dy, float dz,
                          std::vector<float>& verts);
};
