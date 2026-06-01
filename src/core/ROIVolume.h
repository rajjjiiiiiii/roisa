#pragma once
// ROIVolume.h — Core data model for the ROI tool
//
// Holds the display-space float volume and int16 mask.  All segmentation
// algorithms operate on the display-space data; the mask is resampled back to
// original image space only when saving.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <itkImage.h>

class ROIVolume
{
public:
    // ── ITK type aliases ──────────────────────────────────────────────────────
    using FloatImage3 = itk::Image<float,   3>;
    using Int16Image3 = itk::Image<int16_t, 3>;
    using Uint8Image3 = itk::Image<uint8_t, 3>;
    using FloatPtr    = FloatImage3::Pointer;
    using Int16Ptr    = Int16Image3::Pointer;
    using Uint8Ptr    = Uint8Image3::Pointer;

    static constexpr int TARGET_SIZE = 256;   // isotropic display resolution
    static constexpr int MAX_LABELS  = 255;
    static constexpr int MAX_UNDO    = 30;

    ROIVolume()  = default;
    ~ROIVolume() = default;

    // ── I/O ───────────────────────────────────────────────────────────────────

    /// Load NIfTI (.nii / .nii.gz) or a DICOM folder.
    /// Resamples to isotropic TARGET_SIZE for display.
    /// seriesUID selects a specific DICOM series when the folder has several.
    bool load(const std::string& path, const std::string& seriesUID = "");

    /// Path of the first DICOM file used during the last load (empty for NIfTI).
    const std::string& firstDicomFile() const { return m_firstDicomFile; }

    /// Resample mask back to original image space and write as NIfTI.
    bool saveMask(const std::string& outPath) const;

    /// Load an existing mask NIfTI (original-space) and resample to display.
    bool loadMask(const std::string& maskPath);

    /// Write a binary (display-space) NIfTI for a single label.  Used by the
    /// batch label export.  Returns false on failure.
    bool saveLabelBinary(int label, const std::string& path) const;

    /// Labels currently present in the mask (excluding 0).
    std::vector<int> presentLabels() const;

    /// Save / load the mask in display space exactly (for session restore).
    bool saveMaskRaw(const std::string& path) const;
    bool loadMaskRaw(const std::string& path);

    // ── State ─────────────────────────────────────────────────────────────────
    bool isLoaded() const { return static_cast<bool>(m_displayImg); }

    int nx() const;   // display-space X size
    int ny() const;   // display-space Y size
    int nz() const;   // display-space Z size

    // ── Intensity window ──────────────────────────────────────────────────────
    float vmin() const { return m_vmin; }
    float vmax() const { return m_vmax; }
    void  setWindow(float lo, float hi) { m_vmin = lo; m_vmax = hi; }
    void  resetWindow();   // recompute from 1%/99% percentiles + notifyChange

    // ── Fusion display state (per-layer) ───────────────────────────────────────
    int   colormap()    const { return m_colormap; }
    void  setColormap(int cm) { m_colormap = cm; }
    float fusionAlpha() const { return m_fusionAlpha; }
    void  setFusionAlpha(float a) { m_fusionAlpha = a; }

    /// Resample this volume's display image onto `ref`'s display grid so the
    /// two line up for fusion overlay.  Returns null if either isn't loaded.
    FloatPtr resampleDisplayTo(const ROIVolume* ref) const;

    /// Extract an axis slice from a raw display-space buffer (same layout as
    /// getIntensitySlice).  Used to pull aligned overlay slices for fusion.
    static void sliceFromBuffer(const float* buf, int NX, int NY, int NZ,
                                int axis, int idx, std::vector<float>& dst);

    /// uint8 variant — used for the threshold-preview overlay.
    static void sliceFromBufferU8(const uint8_t* buf, int NX, int NY, int NZ,
                                  int axis, int idx, std::vector<uint8_t>& dst);

    /// Fill `label` on empty slices between drawn slices along `axis` (SDF blend).
    /// Returns the number of slices filled.
    int interpolateLabel(int label, int axis);

    // ── Spacing ───────────────────────────────────────────────────────────────
    double voxelSpacingMm() const;   // isotropic spacing of display volume

    // ── Label statistics ──────────────────────────────────────────────────────
    struct LabelStats {
        int    label{0};
        int    voxelCount{0};
        double volumeMm3{0.0};
        float  meanIntensity{0.f};
        float  stdIntensity{0.f};
        int    bboxX0{0}, bboxY0{0}, bboxZ0{0};
        int    bboxX1{-1}, bboxY1{-1}, bboxZ1{-1};
    };
    LabelStats              computeStats(int label) const;
    std::vector<LabelStats> computeAllStats() const;
    std::array<double,3>    labelCentroid(int label) const;

    // ── Slice propagation ─────────────────────────────────────────────────────
    /// Copy label from axisIdx to axisIdx+direction (clamped). Returns voxels set.
    int propagateLabel(int label, int axis, int axisIdx, int direction);

    // ── CSV export ────────────────────────────────────────────────────────────
    bool exportStatsCSV(const std::string& path) const;

    // ── Orientation labels ────────────────────────────────────────────────────
    /// {top, bottom, left, right} anatomical labels for a slice view axis.
    std::array<std::string,4> sliceOrientLabels(int axis) const;

    // ── Isotropic resampling ──────────────────────────────────────────────────
    /// Re-sample the original image to isotropic voxels at spacingMm
    /// (0 = auto: use minimum original spacing).  Replaces the display image
    /// and resets the mask.  Recommended before VTK volume rendering.
    bool resampleToIsotropicSpacing(float spacingMm = 0.f);

    // ── Image registration ────────────────────────────────────────────────────
    /// Rigid-register movingPath to current image; replace display volume.
    /// The mask is preserved. Returns false on error.
    bool loadRegisteredImage(const std::string& movingPath);

    /// Register THIS (moving) volume to `fixed` and resample its display image
    /// into the fixed display grid so it aligns for fusion overlay.
    /// mode: 0=Rigid (Euler3D)  1=Affine  2=Deformable (BSpline).
    /// The pre-registration image is kept so resetRegistration() can restore it.
    bool registerTo(const ROIVolume* fixed, int mode, int iterations = 100);

    /// Pure registration: aligns `moving` to `fixed`, returns the resampled
    /// image (null on failure).  No shared state — safe to call off-thread.
    static FloatPtr registerImages(FloatPtr moving, FloatPtr fixed,
                                   int mode, int iterations);

    /// Ensure the pre-registration backup exists and return the source image to
    /// register (the backup).  Call on the GUI thread before threaded work.
    FloatPtr ensureBackupAndMovingSource();

    /// Adopt a registered/resampled image as this volume's display image.
    /// Call on the GUI thread after threaded registration completes.
    void applyRegisteredImage(FloatPtr img);

    /// Apply a manual rigid transform (mm translation + degree rotation) to the
    /// original moving image and resample into `fixed`'s display grid.
    bool applyManualTransform(const ROIVolume* fixed,
                              double tx, double ty, double tz,
                              double rxDeg, double ryDeg, double rzDeg);

    /// Restore the original (pre-registration) display image.
    bool resetRegistration();
    bool isRegistered() const { return static_cast<bool>(m_displayBackup); }

    // ── Voxel access (display space, x/y/z indexing) ──────────────────────────
    float   getIntensity(int x, int y, int z) const;
    int16_t getMaskLabel (int x, int y, int z) const;
    void    setMaskLabel (int x, int y, int z, int16_t label);

    // ── Slice data for rendering ───────────────────────────────────────────────
    // axis: 0 = sagittal (constant x), 1 = coronal (constant y), 2 = axial (const z)
    // Slice layout:
    //   axis 0: rows = ny, cols = nz   → pixel(row,col) = vol[idx, row, col]
    //   axis 1: rows = nx, cols = nz   → pixel(row,col) = vol[row, idx, col]
    //   axis 2: rows = nx, cols = ny   → pixel(row,col) = vol[row, col, idx]
    int sliceRows(int axis) const;
    int sliceCols(int axis) const;
    void getIntensitySlice(int axis, int idx, std::vector<float>&   dst) const;
    void getMaskSlice     (int axis, int idx, std::vector<int16_t>& dst) const;

    /// Projection slice (same layout as getIntensitySlice).
    /// mode: 0=single 1=MIP(max) 2=MinIP(min); slab: half-width (0 = full volume).
    void getImageSliceProj(int axis, int idx, int mode, int slab,
                           std::vector<float>& dst) const;

    // ── Raw ITK access (for algorithm implementations) ─────────────────────────
    FloatPtr  displayImage() const { return m_displayImg; }
    Int16Ptr  maskImage()    const { return m_mask; }

    // ── Bulk mask write (used by algorithms after computing new labels) ─────────
    /// Overwrite the mask with the contents of newMask (must match display size).
    void replaceMask(Int16Ptr newMask);

    // ── Mask helpers ──────────────────────────────────────────────────────────
    /// Clear voxels with the given label; pass -1 to clear all.
    void clearLabel(int label);

    // ── Undo ──────────────────────────────────────────────────────────────────
    struct UndoEntry {
        std::vector<std::array<int,3>> indices;
        std::vector<int16_t>           oldValues;
    };

    /// Push an undo entry.  Call *before* modifying the mask.
    void pushUndo(std::vector<std::array<int,3>> indices,
                  std::vector<int16_t>           oldValues);

    /// Convenience: capture every voxel currently assigned `label` as undo.
    void pushUndoForLabel(int label);

    /// Capture the entire mask as a single undo entry.
    void pushUndoAll();

    bool undo();
    bool canUndo() const { return !m_history.empty(); }

    // ── Change notification ────────────────────────────────────────────────────
    /// Callback fires whenever the mask changes (e.g., to trigger a repaint).
    void setChangeCallback(std::function<void()> cb) { m_onChange = std::move(cb); }
    void notifyChange();

    /// Suspend change-callback firing (e.g. during background segmentation so
    /// algorithms don't touch the GUI from a worker thread).
    void setNotifyEnabled(bool e) { m_notifyEnabled = e; }

private:
    FloatPtr m_origImg;      // original image (full resolution, for save)
    FloatPtr m_displayImg;   // float32, isotropically resampled to TARGET_SIZE
    FloatPtr m_displayBackup;// pre-registration display image (for Reset)
    Int16Ptr m_mask;         // int16, same grid as m_displayImg

    float       m_vmin{0.f};
    float       m_vmax{1.f};
    int         m_colormap{0};       // 0=gray 1=hot 2=cool 3=viridis
    float       m_fusionAlpha{0.6f}; // opacity when composited as an overlay
    std::string m_firstDicomFile;

    std::deque<UndoEntry>  m_history;
    std::function<void()>  m_onChange;
    bool                   m_notifyEnabled{true};

    // Internal helpers
    FloatPtr loadNiftiOrMeta(const std::string& path);
    FloatPtr loadDicomSeries(const std::string& dir, const std::string& uid = "");
    FloatPtr resampleIsotropic(FloatPtr img, int targetSize);
    Int16Ptr createMask(FloatPtr ref);     // zero-filled, same grid as ref
    Int16Ptr resampleMaskToRef(Int16Ptr mask, FloatPtr ref); // NN interp
    void     computeWindow();
};
