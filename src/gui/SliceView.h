#pragma once
// SliceView.h — Single orthogonal slice panel with zoom, pan, colormap,
//               orientation labels, W/L drag, label visibility, interpolation,
//               and measurement overlay (ruler, angle, circle area).

#include <QWidget>
#include <QStringList>
#include <QPoint>
#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "../core/ROIVolume.h"

class SliceView : public QWidget
{
    Q_OBJECT

public:
    enum Colormap { GRAY=0, HOT=1, COOL=2, VIRIDIS=3 };

    // ── Fusion overlay layer (intensity already resampled to REF grid) ──────────
    struct FusionLayer {
        ROIVolume::FloatPtr arr;          // display image on the REF grid
        int   colormap{HOT};
        float alpha{0.6f};
        float wmin{0.f}, wmax{1.f};
    };

    // ── Measurement types ──────────────────────────────────────────────────────
    enum class MeasureMode { None=0, Ruler=1, Angle=2, Circle=3 };

    /// A point in normalised slice image coordinates [0,1]×[0,1].
    struct MeasurePt { float u{0.f}, v{0.f}; };

    /// One completed (or in-progress) measurement.
    struct Measurement {
        MeasureMode type    {MeasureMode::Ruler};
        int         sliceIdx{0};
        std::vector<MeasurePt> pts;   // Ruler:2, Angle:3, Circle:2(centre+edge)
        bool        complete{false};
        QString     valueStr;          // "12.3 mm", "45.2°", "78.5 mm²"
    };

    explicit SliceView(int axis, QWidget* parent = nullptr);

    void setVolume(ROIVolume* vol);
    void setSliceIndex(int idx);
    int  sliceIndex() const { return m_sliceIdx; }
    int  axis()       const { return m_axis; }

    // Crosshair coords in the other two dimensions
    void setCrosshair(int a, int b);

    // ── Display options ────────────────────────────────────────────────────────
    void setInterpolate(bool on)       { m_interpolate = on; update(); }
    void setColormap(int cm)           { m_colormap = cm; update(); }
    int  colormap() const              { return m_colormap; }
    void setOverlayAlpha(float alpha)  { m_overlayAlpha = alpha; update(); }
    void setBaseVisible(bool on)       { m_baseVisible = on; update(); }
    void setOverlays(std::vector<FusionLayer> ov) { m_overlays = std::move(ov); update(); }
    void setLabelVisible(int lbl, bool v)  { if(lbl>=0&&lbl<256) { m_labelVis[lbl]=v; update(); } }
    void setAllLabelsVisible(bool v)   { m_labelVis.fill(v); update(); }
    void setShowInfoOverlay(bool on)   { m_showInfoOverlay = on; update(); }
    void setProjectionMode(int mode)   { m_projMode = mode; update(); }
    void setSlab(int slab)             { m_slab = slab; update(); }
    void setShowColorbar(bool on)      { m_showColorbar = on; update(); }
    void setPreviewBuffer(const uint8_t* buf) { m_previewBuf = buf; update(); }
    void setPolygonMode(bool on) { m_polygonMode = on; m_polyWidget.clear(); m_polyVoxel.clear(); update(); }
    void resetZoom();

    // ── Measurement API ────────────────────────────────────────────────────────
    void setMeasureMode(MeasureMode mode);
    void clearMeasurements();
    const std::vector<Measurement>& measurementsForSlice(int idx) const;

    /// All completed measurement value strings across slices.
    QStringList measurements() const {
        QStringList out;
        for (const auto& kv : m_measurements)
            for (const auto& m : kv.second)
                if (m.complete) out << m.valueStr;
        return out;
    }

signals:
    void sliceClicked (int x, int y, int z);
    void sliceDragged (int x, int y, int z);
    void sliceReleased();
    void scrolled(int delta);
    void measurementAdded(QString description);
    void polygonClosed(int axis, const std::vector<std::array<int,3>>& voxels);

protected:
    void paintEvent       (QPaintEvent*)  override;
    void mousePressEvent  (QMouseEvent*)  override;
    void mouseMoveEvent   (QMouseEvent*)  override;
    void mouseReleaseEvent(QMouseEvent*)  override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent       (QWheelEvent*)  override;

private:
    ROIVolume* m_vol{nullptr};
    int        m_axis;
    int        m_sliceIdx{0};
    int        m_crossA{0}, m_crossB{0};

    // ── Measurement state ──────────────────────────────────────────────────────
    MeasureMode  m_measureMode{MeasureMode::None};
    std::map<int, std::vector<Measurement>> m_measurements; // keyed by sliceIdx
    Measurement  m_pending;      // in-progress measurement
    bool         m_measPending{false};
    MeasurePt    m_measCursor;   // live cursor position for preview

    QPointF  widgetToNorm(QPointF w) const;
    QPointF  normToWidget(QPointF n) const;
    float    normDistMm(MeasurePt a, MeasurePt b) const;
    void     computeMeasureValue(Measurement& m) const;
    void     paintMeasurements(QPainter& p) const;

    // Left-button drag (paint/segment)
    bool m_leftDragging{false};

    // Middle/right drag for pan and W/L
    bool m_midDragging{false};
    int  m_midStartX{0}, m_midStartY{0};
    int  m_panStartX{0}, m_panStartY{0};

    bool  m_rightDragging{false};
    int   m_rightStartX{0}, m_rightStartY{0};
    float m_dragStartVmin{0.f}, m_dragStartVmax{0.f};

    // Zoom / pan state
    double m_scale{1.0};
    int    m_panX{0}, m_panY{0};

    // Display options
    bool  m_interpolate{false};
    int   m_colormap{GRAY};
    float m_overlayAlpha{1.0f};
    bool  m_showInfoOverlay{true};
    bool  m_baseVisible{true};                 // REF base layer composited?
    int   m_projMode{0};                       // 0=slice 1=MIP 2=MinIP
    int   m_slab{0};                           // projection half-width
    bool  m_showColorbar{false};
    const uint8_t* m_previewBuf{nullptr};      // threshold-preview volume (not owned)
    bool m_polygonMode{false};
    std::vector<QPoint>              m_polyWidget;  // vertices in widget pixels
    std::vector<std::array<int,3>>   m_polyVoxel;   // vertices in (x,y,z) voxels
    std::vector<FusionLayer> m_overlays;       // fusion overlays on REF grid
    std::array<bool,256> m_labelVis;   // label 0..255 visibility

    mutable std::vector<float>   m_intensityBuf;
    mutable std::vector<int16_t> m_maskBuf;

    // Return the letterboxed+zoomed image rect within this widget
    QRect imageRect() const;

    // Map widget pixel → display-space voxel; false if outside
    bool widgetToVoxel(int wx, int wy, int& vx, int& vy, int& vz) const;

    // Apply colormap to a normalised [0,1] value → RGB
    static void applyColormap(int cm, float t, int& r, int& g, int& b);

    // Cycled RGBA colour for label index
    static std::array<uint8_t,4> labelColor(int label);
};
