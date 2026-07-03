// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_RENDER_PRODUCER_H_
#define UI_OZONE_PLATFORM_QNX_QNX_RENDER_PRODUCER_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "base/files/scoped_file.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"

// Include EGL base types before EGL extension types.
// This matches the pattern used by ui/gl/gl_surface_egl.h and
// ui/ozone/platform/drm/gpu/gbm_surfaceless.h: include egl.h first,
// then eglext.h.  The Chromium build system maps <EGL/*.h> to
// third_party/khronos/EGL/*.h (or third_party/angle/include/EGL/*.h)
// so these are safe to use as system-style includes.
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace gl {
class GLDisplayEGL;
class GLSurface;
class GLContext;
}  // namespace gl

namespace ui {

// Forward declarations.
class QnxSurfaceFactoryOzone;

// ====================================================================
// QnxDmaBufPlane — frame metadata for one DMAbuf plane
// ====================================================================
// Compatible with ui.ozone.qnx.mojom.QnxDmaBufPlane.
// Uses base::ScopedFD so fds are automatically closed on destruction.
struct QnxDmaBufPlane {
  // Owned DMAbuf file descriptor.  Wraps the fd from
  // eglExportDMABUFImageMESA so it is closed when this struct is destroyed.
  base::ScopedFD fd;

  // Bytes per scan line (row pitch) for this plane.
  uint32_t stride = 0;

  // Byte offset from start of DMAbuf where this plane begins.
  uint64_t offset = 0;

  // Total size of this plane in bytes.
  uint64_t size = 0;
};

// ====================================================================
// QnxDmaBufFrame — complete DMAbuf frame metadata
// ====================================================================
// Compatible with ui.ozone.qnx.mojom.QnxDmaBufFrame.
// This struct carries everything the Browser process needs to import and
// display a GPU-rendered frame via eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT).
struct QnxDmaBufFrame {
  // Stable widget ID this frame targets (matches QnxWindowManager allocation).
  gfx::AcceleratedWidget widget = gfx::kNullAcceleratedWidget;

  // Generation counter.  Frames with stale generation are discarded by browser.
  uint32_t generation = 0;

  // Frame pixel dimensions.
  uint32_t width = 0;
  uint32_t height = 0;

  // FourCC code identifying pixel format (e.g. 0x34325241 = AR24).
  // Set by eglExportDMABUFImageQueryMESA.
  uint32_t fourcc = 0;

  // DRM modifier describing buffer layout. 0 = linear.
  uint64_t modifier = 0;

  // One entry per DMAbuf plane. Plane count and layout must match |fourcc|.
  std::vector<QnxDmaBufPlane> planes;
};

// ====================================================================
// QnxRenderProducer — per-widget GPU render producer
// ====================================================================
// Owned by the GPU process.  One instance per widget+generation pair.
//
// Responsibilities:
// - Create and manage GPU-side EGL display and GLES2 context.
// - Manage a pool of DRM EGLImages (via eglCreateDRMImageMESA).
// - Export DMAbuf frames via eglExportDMABUFImageMESA when called.
// - Provide QnxDmaBufFrame metadata + ScopedFDs ready for Mojo submission.
//
// Phase 5 scope:
// - Compile-safe scaffold with EGL extension probing and function-pointer
//   resolution.
// - Stubbed CreateExportFrame() that returns a QnxDmaBufFrame with
//   demo metadata; runtime wiring of Mojo SubmitFrame binding is deferred.
// - NOT wired to QnxGpuHost Mojo client yet.
//
// GPU crash/restart behavior:
// - QnxRenderProducer is recreated when GPU process restarts.
// - Browser window survives (owned by browser process).
// - QnxRenderProducer does not own or reference screen_window_t.
class QnxRenderProducer {
 public:
  // Construction requires the owning QnxSurfaceFactoryOzone (for EGL display).
  // Each producer is associated with one widget+generation pair.
  QnxRenderProducer(QnxSurfaceFactoryOzone* surface_factory,
                    gfx::AcceleratedWidget widget,
                    uint32_t generation,
                    const gfx::Size& size);
  ~QnxRenderProducer();

  QnxRenderProducer(const QnxRenderProducer&) = delete;
  QnxRenderProducer& operator=(const QnxRenderProducer&) = delete;

  // ---- Accessors ----

  gfx::AcceleratedWidget widget() const { return widget_; }
  uint32_t generation() const { return generation_; }
  const gfx::Size& size() const { return size_; }
  bool is_valid() const { return is_valid_; }
  const std::string& init_error() const { return init_error_; }

  // ---- EGL extension probing ----

  // Returns a human-readable summary of which DMAbuf/EGL extensions are
  // present and which function pointers were resolved.
  std::string ExtensionReport() const;

  // Returns true if the minimum set of extensions for DMAbuf export is
  // present: EGL_MESA_drm_image + EGL_MESA_image_dma_buf_export.
  bool CanExportDmaBuf() const;

  // ---- Render producer API ----

  // Probe EGL extensions and resolve DMAbuf export function pointers.
  // Call this once after construction or EGL display initialization.
  // Returns true if all required function pointers are resolved.
  bool Initialize();

  // Creates a DMAbuf-exportable DRM image of the given size.
  // Returns the EGLImage handle, or EGL_NO_IMAGE_KHR on failure.
  // The returned image must be destroyed via DestroyDRMImage() when done.
  EGLImageKHR CreateDRMImage(const gfx::Size& size, std::string* out_error);

  // Destroys a DRM image created by CreateDRMImage().
  // Returns true on success.
  bool DestroyDRMImage(EGLImageKHR image);

  // Queries DMAbuf metadata from a DRM image created by CreateDRMImage().
  // Returns true on success and fills |fourcc|, |n_planes|, |modifier|.
  bool QueryDmaBufMetadata(EGLImageKHR image,
                           uint32_t* fourcc,
                           int* n_planes,
                           uint64_t* modifier);

  // Exports DMAbuf plane fds from a DRM image created by CreateDRMImage().
  // |fds_out| must be sized to at least |n_planes| elements (use
  // QueryDmaBufMetadata to determine plane count).
  // |strides_out| and |offsets_out| must also be sized to |n_planes|.
  // Ownership of the returned fds is transferred to the caller.
  // Returns true if all planes were exported successfully.
  bool ExportDmaBufImage(EGLImageKHR image,
                         int n_planes,
                         base::ScopedFD* fds_out,
                         int* strides_out,
                         int* offsets_out);

  // Creates and exports one complete DMAbuf frame.
  // This is the primary API for the Mojo SubmitFrame pipeline.
  //
  // Phase 5 status: this method returns a QnxDmaBufFrame with demo/placeholder
  // data and stubbed fd handling.  Runtime EGL/GLES2 wiring is deferred.
  // Full implementation will:
  //   1. glFlush + glFinish to ensure rendering is complete.
  //   2. eglExportDMABUFImageMESA to get real DMAbuf fds.
  //   3. Populate QnxDmaBufFrame with real metadata from
  //      eglExportDMABUFImageQueryMESA.
  //   4. Return via Mojo SubmitFrame once binding is wired.
  std::pair<QnxDmaBufFrame, std::string> CreateExportFrame();

  // Returns the GPU-side EGL display used by this producer.
  EGLDisplay egl_display() const { return egl_display_; }

 private:
  // Resolve a single EGL function pointer by name.
  // Logs a warning if the function is not found.
  template <typename Fn>
  Fn ResolveEGL(const char* name);

  // Probe EGL extensions and log the report.
  void ProbeExtensions();

  // GPU-side EGL display.  Initialized from the surface factory's GL display.
  EGLDisplay egl_display_ = EGL_NO_DISPLAY;

  // QNX Screen is NOT used by the GPU render producer.
  // (Screen ownership is browser-side only.)

  // Widget and generation this producer is attached to.
  const gfx::AcceleratedWidget widget_;
  const uint32_t generation_;
  const gfx::Size size_;

  // True if Initialize() succeeded and CanExportDmaBuf() is true.
  bool is_valid_ = false;

  // Human-readable error from initialization (empty on success).
  std::string init_error_;

  // ---- EGL function pointers (DMAbuf export path) ----

  // eglCreateDRMImageMESA — creates a Mesa-internal DRM buffer as EGLImage.
  using EglCreateDRMImageMESAFn = EGLImageKHR(
      EGLDisplay dpy, const EGLint* attrib_list);
  EglCreateDRMImageMESAFn* egl_create_drm_image_mesa_ = nullptr;

  // eglExportDMABUFImageMESA — exports DMAbuf plane fds from a DRM EGLImage.
  using EglExportDMABUFImageMESAFn = EGLBoolean(
      EGLDisplay dpy, EGLImageKHR image, EGLint* fds,
      EGLint* strides, EGLint* offsets);
  EglExportDMABUFImageMESAFn* egl_export_dma_buf_image_mesa_ = nullptr;

  // eglExportDMABUFImageQueryMESA — queries fourcc/planes/modifier.
  using EglExportDMABUFImageQueryMESAFn = EGLBoolean(
      EGLDisplay dpy, EGLImageKHR image, EGLint* fourcc,
      EGLint* n_planes, EGLuint64KHR* modifier);
  EglExportDMABUFImageQueryMESAFn* egl_export_dma_buf_image_query_mesa_ =
      nullptr;

  // eglDestroyImageKHR — destroys an EGLImage.
  using EglDestroyImageKHRFn = EGLBoolean(EGLDisplay dpy, EGLImageKHR image);
  EglDestroyImageKHRFn* egl_destroy_image_khr_ = nullptr;

  // ---- Extension presence flags ----

  bool has_egl_mesa_drm_image_ = false;
  bool has_egl_mesa_image_dma_buf_export_ = false;
  bool has_egl_ext_image_dma_buf_import_ = false;
  bool has_egl_ext_image_dma_buf_import_modifiers_ = false;
  bool has_khr_gl_texture_2d_ = false;
  bool has_khr_surfaceless_context_ = false;
  bool has_gl_oes_egl_image_ = false;

  // Raw pointer to the owning surface factory (non-owning).
  raw_ptr<QnxSurfaceFactoryOzone> surface_factory_;
};

// ====================================================================
// QnxRenderProducerManager — manages per-widget producer instances
// ====================================================================
// Owned by QnxSurfaceFactoryOzone or QnxGpuService.  Provides
// factory/lookup for QnxRenderProducer instances keyed by widget+generation.

// Forward declaration (defined in qnx_surface_factory.h).
class QnxSurfaceFactoryOzone;

class QnxRenderProducerManager {
 public:
  // Constructs a manager that creates producers using |surface_factory|.
  // |surface_factory| must outlive this object.
  explicit QnxRenderProducerManager(QnxSurfaceFactoryOzone* surface_factory);
  ~QnxRenderProducerManager();

  QnxRenderProducerManager(const QnxRenderProducerManager&) = delete;
  QnxRenderProducerManager& operator=(const QnxRenderProducerManager&) = delete;

  // Creates a producer for |widget| at |generation| with the given |size|.
  // If a producer already exists for the same widget+generation, returns
  // the existing one (singleton per widget+generation).
  QnxRenderProducer* GetOrCreateProducer(gfx::AcceleratedWidget widget,
                                        uint32_t generation,
                                        const gfx::Size& size);

  // Removes a producer by widget+generation key.
  void RemoveProducer(gfx::AcceleratedWidget widget, uint32_t generation);

  // Looks up a producer.  Returns nullptr if not found.
  QnxRenderProducer* GetProducer(gfx::AcceleratedWidget widget,
                                 uint32_t generation);

  // Removes all producers (e.g. on GPU process shutdown).
  void RemoveAllProducers();

 private:
  using ProducerKey = std::pair<gfx::AcceleratedWidget, uint32_t>;

  // Non-owning pointer to the surface factory.  Must outlive this manager.
  raw_ptr<QnxSurfaceFactoryOzone> surface_factory_;

  std::map<ProducerKey, std::unique_ptr<QnxRenderProducer>> producers_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_RENDER_PRODUCER_H_
