// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 5: GPU-side QNX render producer scaffold.
// See docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md.
//
// This file implements the GPU-side DMAbuf export pipeline using the
// Phase 1B-proven path:
//   eglCreateDRMImageMESA → [render] → eglExportDMABUFImageMESA
//
// The QnxDmaBufFrame metadata and base::ScopedFD fds are ready for
// Mojo QnxGpuHost.SubmitFrame once binding is wired.

#include "ui/ozone/platform/qnx/qnx_render_producer.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

#include <dlfcn.h>

#include <memory>
#include <vector>

#include "base/logging.h"
#include <GLES2/gl2.h>

#include "base/notreached.h"
#include "ui/ozone/platform/qnx/qnx_surface_factory.h"

// EGL function pointer typedefs (mirrored in header for clarity).
typedef EGLImageKHR(EGLAPIENTRYP PFNEGLCREATEDRMIMAGEMESAPROC)(
    EGLDisplay dpy, const EGLint* attrib_list);
typedef EGLBoolean(EGLAPIENTRYP PFNEGLEXPORTDMABUFIMAGEMESAPROC)(
    EGLDisplay dpy, EGLImageKHR image, EGLint* fds, EGLint* strides,
    EGLint* offsets);
typedef EGLBoolean(EGLAPIENTRYP PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC)(
    EGLDisplay dpy, EGLImageKHR image, EGLint* fourcc, EGLint* n_planes,
    EGLuint64KHR* modifier);
typedef EGLBoolean(EGLAPIENTRYP PFNEGLDESTROYIMAGEKHRPROC)(
    EGLDisplay dpy, EGLImageKHR image);
typedef EGLImageKHR(EGLAPIENTRYP PFNEGLCREATEIMAGEKHRPROC)(
    EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer,
    const EGLint* attrib_list);

// GL function pointer typedefs.
typedef void(EGLAPIENTRYP PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(
    GLenum target, void* image);

namespace ui {

namespace {

// Returns a human-readable name for an EGL error code.
const char* EglErrorName(EGLint err) {
  switch (err) {
    case EGL_SUCCESS:
      return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
      return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
      return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
      return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
      return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:
      return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:
      return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE:
      return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
      return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH:
      return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP:
      return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
      return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:
      return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:
      return "EGL_BAD_SURFACE";
    case EGL_CONTEXT_LOST:
      return "EGL_CONTEXT_LOST";
    default:
      return "(unknown EGL error)";
  }
}

// Returns a human-readable name for a GL error code.
const char* GlErrorName(GLenum err) {
  switch (err) {
    case GL_NO_ERROR:
      return "GL_NO_ERROR";
    case GL_INVALID_ENUM:
      return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
      return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
      return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY:
      return "GL_OUT_OF_MEMORY";
    default:
      return "(unknown GL error)";
  }
}

// Returns a FourCC string for logging.
std::string FourccToString(uint32_t fourcc) {
  std::string s;
  s.push_back(static_cast<char>((fourcc >> 0) & 0xff));
  s.push_back(static_cast<char>((fourcc >> 8) & 0xff));
  s.push_back(static_cast<char>((fourcc >> 16) & 0xff));
  s.push_back(static_cast<char>((fourcc >> 24) & 0xff));
  return s;
}

}  // namespace

// ======================================================================
// QnxRenderProducer
// ======================================================================

QnxRenderProducer::QnxRenderProducer(QnxSurfaceFactoryOzone* surface_factory,
                                     gfx::AcceleratedWidget widget,
                                     uint32_t generation,
                                     const gfx::Size& size)
    : widget_(widget), generation_(generation), size_(size) {
  if (!surface_factory) {
    init_error_ = "QnxRenderProducer: surface_factory is null";
    LOG(ERROR) << init_error_;
    return;
  }
  surface_factory_ = surface_factory;

  // Get the EGL display from the GL display.
  // The GL display is initialized by GLOzoneEGL::InitializeGLOneOffPlatform.
  gl::GLDisplayEGL* gl_display_egl =
      gl::GLDisplayEGL::GetDisplayForCurrentContext();
  if (!gl_display_egl) {
    // Fallback: use eglGetDisplay directly.
    // This is the same pattern used by the Phase 1B probes.
    egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display_ == EGL_NO_DISPLAY) {
      init_error_ = "QnxRenderProducer: eglGetDisplay(EGL_DEFAULT_DISPLAY) "
                    "returned EGL_NO_DISPLAY";
      LOG(ERROR) << init_error_;
      return;
    }
    EGLint maj = 0, min = 0;
    if (!eglInitialize(egl_display_, &maj, &min)) {
      init_error_ = "QnxRenderProducer: eglInitialize failed: " +
                    std::string(EglErrorName(eglGetError()));
      LOG(ERROR) << init_error_;
      return;
    }
    QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer: EGL " << maj << "." << min
               << " initialized from eglGetDisplay fallback";
  } else {
    egl_display_ = gl_display_egl->GetDisplay();
  }

  if (egl_display_ == EGL_NO_DISPLAY) {
    init_error_ = "QnxRenderProducer: EGL display is null";
    LOG(ERROR) << init_error_;
    return;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer: created for widget=" << widget_
             << " generation=" << generation_
             << " size=" << size_.ToString();
}

QnxRenderProducer::~QnxRenderProducer() {
  // EGL display is shared and owned by GLOzoneEGL; do not terminate here.
  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer: destroyed for widget=" << widget_;
}

bool QnxRenderProducer::Initialize() {
  if (!init_error_.empty()) {
    DLOG(ERROR) << "QnxRenderProducer::Initialize: skipped due to "
                   "construction error: " << init_error_;
    return false;
  }

  // ---- Probe EGL extensions ----
  ProbeExtensions();

  // ---- Resolve DMAbuf export function pointers ----
  egl_create_drm_image_mesa_ =
      ResolveEGL<EglCreateDRMImageMESAFn*>("eglCreateDRMImageMESA");
  egl_export_dma_buf_image_mesa_ =
      ResolveEGL<EglExportDMABUFImageMESAFn*>("eglExportDMABUFImageMESA");
  egl_export_dma_buf_image_query_mesa_ =
      ResolveEGL<EglExportDMABUFImageQueryMESAFn*>(
          "eglExportDMABUFImageQueryMESA");
  egl_destroy_image_khr_ =
      ResolveEGL<EglDestroyImageKHRFn*>("eglDestroyImageKHR");

  if (!CanExportDmaBuf()) {
    init_error_ =
        "QnxRenderProducer::Initialize: required DMAbuf export extensions "
        "or function pointers are missing. See ExtensionReport() for details.";
    LOG(ERROR) << init_error_;
    return false;
  }

  is_valid_ = true;
  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer::Initialize: success for widget=" << widget_;
  return true;
}

std::string QnxRenderProducer::ExtensionReport() const {
  std::ostringstream oss;
  oss << "QnxRenderProducer extension report (widget=" << widget_ << "):\n";
  oss << "  EGL_MESA_drm_image:          " << (has_egl_mesa_drm_image_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  EGL_MESA_image_dma_buf_export:" << (has_egl_mesa_image_dma_buf_export_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  EGL_EXT_image_dma_buf_import: " << (has_egl_ext_image_dma_buf_import_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  EGL_EXT_image_dma_buf_import_modifiers: " << (has_egl_ext_image_dma_buf_import_modifiers_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  EGL_KHR_gl_texture_2d:        " << (has_khr_gl_texture_2d_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  EGL_KHR_surfaceless_context:  " << (has_khr_surfaceless_context_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  eglCreateDRMImageMESA:        " << (egl_create_drm_image_mesa_ ? "RESOLVED" : "NOT RESOLVED") << "\n";
  oss << "  eglExportDMABUFImageMESA:     " << (egl_export_dma_buf_image_mesa_ ? "RESOLVED" : "NOT RESOLVED") << "\n";
  oss << "  eglExportDMABUFImageQueryMESA:" << (egl_export_dma_buf_image_query_mesa_ ? "RESOLVED" : "NOT RESOLVED") << "\n";
  oss << "  eglDestroyImageKHR:            " << (egl_destroy_image_khr_ ? "RESOLVED" : "NOT RESOLVED") << "\n";
  return oss.str();
}

bool QnxRenderProducer::CanExportDmaBuf() const {
  // Minimum required for Phase 1B-proven Path A:
  // EGL_MESA_drm_image + eglCreateDRMImageMESA + eglExportDMABUFImageMESA.
  return has_egl_mesa_drm_image_ && has_egl_mesa_image_dma_buf_export_ &&
         egl_create_drm_image_mesa_ != nullptr &&
         egl_export_dma_buf_image_mesa_ != nullptr;
}

void QnxRenderProducer::ProbeExtensions() {
  const char* egl_exts =
      eglQueryString(egl_display_, EGL_EXTENSIONS);
  if (egl_exts) {
    QNX_GPU_TRACE_LOG(INFO) << "EGL_EXTENSIONS (" << strlen(egl_exts)
               << " chars): " << egl_exts;
  } else {
    QNX_GPU_TRACE_LOG(INFO) << "EGL_EXTENSIONS: eglQueryString returned null";
  };

  has_egl_mesa_drm_image_ =
      egl_exts && strstr(egl_exts, "EGL_MESA_drm_image") != nullptr;
  has_egl_mesa_image_dma_buf_export_ =
      egl_exts && strstr(egl_exts, "EGL_MESA_image_dma_buf_export") != nullptr;
  has_egl_ext_image_dma_buf_import_ =
      egl_exts && strstr(egl_exts, "EGL_EXT_image_dma_buf_import") != nullptr;
  has_egl_ext_image_dma_buf_import_modifiers_ =
      egl_exts && strstr(egl_exts, "EGL_EXT_image_dma_buf_import_modifiers") != nullptr;
  has_khr_gl_texture_2d_ =
      egl_exts && strstr(egl_exts, "EGL_KHR_gl_texture_2d") != nullptr;
  has_khr_surfaceless_context_ =
      egl_exts && strstr(egl_exts, "EGL_KHR_surfaceless_context") != nullptr;
}

template <typename Fn>
Fn QnxRenderProducer::ResolveEGL(const char* name) {
  // eglGetProcAddress returns a function pointer; cast through the
  // __eglMustCastToProperFunctionPointerType intermediate on QNX.
  auto proc = reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
      eglGetProcAddress(name));
  if (!proc) {
    DLOG(WARNING) << "QnxRenderProducer: eglGetProcAddress(\"" << name
                  << "\") returned null";
    return nullptr;
  }
  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer: resolved " << name;
  return reinterpret_cast<Fn>(proc);
}

EGLImageKHR QnxRenderProducer::CreateDRMImage(const gfx::Size& size,
                                             std::string* out_error) {
  if (out_error)
    out_error->clear();

  if (!is_valid_) {
    if (out_error)
      *out_error = "QnxRenderProducer: not initialized";
    return EGL_NO_IMAGE_KHR;
  }

  if (!egl_create_drm_image_mesa_) {
    if (out_error)
      *out_error = "QnxRenderProducer: eglCreateDRMImageMESA not resolved";
    return EGL_NO_IMAGE_KHR;
  }

  // Attribute list for eglCreateDRMImageMESA (Phase 1B-proven Path A):
  // - EGL_DRM_BUFFER_FORMAT_MESA = ARGB32 for 32-bit RGBA.
  // - EGL_DRM_BUFFER_USE_MESA = SCANOUT | SHARE (share for GPU access).
  // - EGL_WIDTH / EGL_HEIGHT = pixel dimensions.
  // This matches the Phase 1B-exportonly probe which produced a valid AR24
  // (0x34325241 = DRM_FORMAT_ARGB8888) DMAbuf fd under QEMU virgl.
  const EGLint attrs[] = {
      EGL_DRM_BUFFER_FORMAT_MESA, EGL_DRM_BUFFER_FORMAT_ARGB32_MESA,
      EGL_DRM_BUFFER_USE_MESA,
      (EGL_DRM_BUFFER_USE_SCANOUT_MESA | EGL_DRM_BUFFER_USE_SHARE_MESA),
      EGL_WIDTH, static_cast<EGLint>(size.width()),
      EGL_HEIGHT, static_cast<EGLint>(size.height()),
      EGL_NONE};

  EGLImageKHR img = egl_create_drm_image_mesa_(egl_display_, attrs);
  if (img == EGL_NO_IMAGE_KHR) {
    EGLint err = eglGetError();
    if (out_error) {
      *out_error = "eglCreateDRMImageMESA failed: " + std::string(EglErrorName(err));
    }
    DLOG(ERROR) << "QnxRenderProducer::CreateDRMImage: failed: " << EglErrorName(err);
    return EGL_NO_IMAGE_KHR;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer::CreateDRMImage: created " << img
             << " size=" << size.width() << "x" << size.height();
  return img;
}

bool QnxRenderProducer::DestroyDRMImage(EGLImageKHR image) {
  if (image == EGL_NO_IMAGE_KHR)
    return true;

  if (!egl_destroy_image_khr_) {
    DLOG(WARNING) << "QnxRenderProducer::DestroyDRMImage: "
                     "eglDestroyImageKHR not resolved; leaking image "
                  << image;
    return false;
  }

  EGLBoolean ok = egl_destroy_image_khr_(egl_display_, image);
  if (!ok) {
    DLOG(ERROR) << "QnxRenderProducer::DestroyDRMImage: failed: "
                << EglErrorName(eglGetError());
    return false;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer::DestroyDRMImage: destroyed " << image;
  return true;
}

bool QnxRenderProducer::QueryDmaBufMetadata(EGLImageKHR image,
                                           uint32_t* fourcc,
                                           int* n_planes,
                                           uint64_t* modifier) {
  if (!is_valid_) {
    DLOG(ERROR) << "QnxRenderProducer::QueryDmaBufMetadata: not initialized";
    return false;
  }

  if (!egl_export_dma_buf_image_query_mesa_) {
    DLOG(ERROR) << "QnxRenderProducer::QueryDmaBufMetadata: "
                   "eglExportDMABUFImageQueryMESA not resolved";
    return false;
  }

  EGLint egFourcc = 0;
  EGLint egPlanes = 0;
  EGLuint64KHR egModifier = 0;

  EGLBoolean ok = egl_export_dma_buf_image_query_mesa_(
      egl_display_, image, &egFourcc, &egPlanes, &egModifier);

  if (!ok) {
    DLOG(ERROR) << "QnxRenderProducer::QueryDmaBufMetadata: "
                   "eglExportDMABUFImageQueryMESA failed: "
                << EglErrorName(eglGetError());
    return false;
  }

  if (fourcc)
    *fourcc = static_cast<uint32_t>(egFourcc);
  if (n_planes)
    *n_planes = egPlanes;
  if (modifier)
    *modifier = static_cast<uint64_t>(egModifier);

  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer::QueryDmaBufMetadata: fourcc=0x" << std::hex
             << egFourcc << " (" << FourccToString(static_cast<uint32_t>(egFourcc))
             << ") planes=" << egPlanes << " modifier=0x" << egModifier;
  return true;
}

bool QnxRenderProducer::ExportDmaBufImage(EGLImageKHR image,
                                         int n_planes,
                                         base::ScopedFD* fds_out,
                                         int* strides_out,
                                         int* offsets_out) {
  if (!is_valid_) {
    DLOG(ERROR) << "QnxRenderProducer::ExportDmaBufImage: not initialized";
    return false;
  }

  if (!egl_export_dma_buf_image_mesa_) {
    DLOG(ERROR) << "QnxRenderProducer::ExportDmaBufImage: "
                   "eglExportDMABUFImageMESA not resolved";
    return false;
  }

  // eglExportDMABUFImageMESA fills the first |n_planes| entries of each array.
  // We allocate on the stack; n_planes is typically 1-4.
  int export_fds[4] = {-1, -1, -1, -1};
  int export_strides[4] = {0, 0, 0, 0};
  int export_offsets[4] = {0, 0, 0, 0};

  EGLBoolean ok = egl_export_dma_buf_image_mesa_(
      egl_display_, image, export_fds, export_strides, export_offsets);

  if (!ok) {
    DLOG(ERROR) << "QnxRenderProducer::ExportDmaBufImage: "
                   "eglExportDMABUFImageMESA failed: "
                << EglErrorName(eglGetError());
    return false;
  }

  int planes_exported = 0;
  for (int i = 0; i < n_planes && i < 4; i++) {
    if (export_fds[i] >= 0) {
      // Take ownership of the fd.  eglExportDMABUFImageMESA transfers ownership;
      // the caller is responsible for closing the fd when done.
      fds_out[i].reset(export_fds[i]);
      if (strides_out)
        strides_out[i] = export_strides[i];
      if (offsets_out)
        offsets_out[i] = export_offsets[i];
      planes_exported++;

      QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer::ExportDmaBufImage: plane " << i
                 << " fd=" << export_fds[i]
                 << " stride=" << export_strides[i]
                 << " offset=" << export_offsets[i];
    } else {
      DLOG(WARNING) << "QnxRenderProducer::ExportDmaBufImage: plane " << i
                    << " has invalid fd=" << export_fds[i];
    }
  }

  if (planes_exported == 0) {
    DLOG(ERROR) << "QnxRenderProducer::ExportDmaBufImage: no valid fds exported";
    return false;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer::ExportDmaBufImage: exported " << planes_exported
             << " planes for widget=" << widget_;
  return true;
}

std::pair<QnxDmaBufFrame, std::string> QnxRenderProducer::CreateExportFrame() {
  QnxDmaBufFrame frame;
  frame.widget = widget_;
  frame.generation = generation_;
  frame.width = static_cast<uint32_t>(size_.width());
  frame.height = static_cast<uint32_t>(size_.height());

  if (!is_valid_) {
    std::string err =
        "QnxRenderProducer::CreateExportFrame: producer not initialized";
    DLOG(ERROR) << err;
    return std::make_pair(std::move(frame), err);
  }

  // ---- Phase 5 scaffold: demo export via eglCreateDRMImageMESA ----
  //
  // Full implementation will:
  //   1. Wait for a render completion signal from the GPU pipeline.
  //   2. Call glFlush() + glFinish() to ensure GPU rendering is complete.
  //   3. Create a DRM EGLImage via CreateDRMImage().
  //   4. Copy/render the GPU framebuffer into the DRM image (GLES2 blit).
  //   5. Query metadata via QueryDmaBufMetadata().
  //   6. Export fds via ExportDmaBufImage().
  //   7. Populate QnxDmaBufFrame with real metadata.
  //   8. Return the frame for Mojo QnxGpuHost.SubmitFrame submission.
  //
  // Phase 5 scaffold: demonstrate the export path with a minimal DRM image
  // creation and export.  Real GPU pipeline wiring is deferred.

  std::string error;

  // Create a DRM image for export.
  EGLImageKHR img = CreateDRMImage(size_, &error);
  if (img == EGL_NO_IMAGE_KHR) {
    DLOG(ERROR) << "QnxRenderProducer::CreateExportFrame: CreateDRMImage failed: "
                << error;
    return std::make_pair(std::move(frame),
                          "CreateDRMImage failed: " + error);
  }

  // Query DMAbuf metadata.
  uint32_t fourcc = 0;
  int n_planes = 0;
  uint64_t modifier = 0;
  if (!QueryDmaBufMetadata(img, &fourcc, &n_planes, &modifier)) {
    error = "QueryDmaBufMetadata failed";
    DestroyDRMImage(img);
    return std::make_pair(std::move(frame), error);
  }

  frame.fourcc = fourcc;
  frame.modifier = modifier;

  // Export DMAbuf fds.
  std::vector<base::ScopedFD> scoped_fds(
      static_cast<size_t>(n_planes));
  int strides[4] = {0, 0, 0, 0};
  int offsets[4] = {0, 0, 0, 0};

  if (!ExportDmaBufImage(img, n_planes, scoped_fds.data(), strides, offsets)) {
    error = "ExportDmaBufImage failed";
    DestroyDRMImage(img);
    return std::make_pair(std::move(frame), error);
  }

  // Populate frame planes.
  frame.planes.resize(static_cast<size_t>(n_planes));
  for (int i = 0; i < n_planes; i++) {
    frame.planes[i].fd = std::move(scoped_fds[i]);
    frame.planes[i].stride = static_cast<uint32_t>(strides[i]);
    frame.planes[i].offset = static_cast<uint64_t>(offsets[i]);
    // Size estimation: stride * height covers the plane data.
    frame.planes[i].size =
        static_cast<uint64_t>(strides[i]) * static_cast<uint64_t>(size_.height());
  }

  // Clean up the DRM image.  The DMAbuf fd has been transferred to the frame.
  DestroyDRMImage(img);

  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducer::CreateExportFrame: widget=" << widget_
             << " size=" << size_.width() << "x" << size_.height()
             << " fourcc=0x" << std::hex << fourcc << " (" << FourccToString(fourcc) << ")"
             << " planes=" << n_planes
             << " modifier=0x" << modifier
             << " frame ready for Mojo SubmitFrame";

  return std::make_pair(std::move(frame), std::string());
}

// ======================================================================
// QnxRenderProducerManager
// ======================================================================

QnxRenderProducerManager::QnxRenderProducerManager(
    QnxSurfaceFactoryOzone* surface_factory)
    : surface_factory_(surface_factory) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducerManager: constructed with surface_factory="
             << static_cast<void*>(surface_factory_);
}

QnxRenderProducerManager::~QnxRenderProducerManager() {
  RemoveAllProducers();
}

QnxRenderProducer* QnxRenderProducerManager::GetOrCreateProducer(
    gfx::AcceleratedWidget widget,
    uint32_t generation,
    const gfx::Size& size) {
  ProducerKey key{widget, generation};
  auto it = producers_.find(key);
  if (it != producers_.end()) {
    QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducerManager: returning existing producer "
                  "for widget="
               << widget << " generation=" << generation;
    return it->second.get();
  }

  // Create a new producer using the surface factory provided at construction.
  // surface_factory_ is guaranteed non-null when constructed via
  // QnxGpuService(QnxSurfaceFactoryOzone*) or equivalent.
  auto producer = std::make_unique<QnxRenderProducer>(
      surface_factory_, widget, generation, size);
  QnxRenderProducer* raw = producer.get();
  producers_.emplace(key, std::move(producer));
  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducerManager: created producer for widget="
             << widget << " generation=" << generation
             << " using surface_factory=" << static_cast<void*>(surface_factory_);
  return raw;
}

void QnxRenderProducerManager::RemoveProducer(gfx::AcceleratedWidget widget,
                                              uint32_t generation) {
  ProducerKey key{widget, generation};
  producers_.erase(key);
  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducerManager: removed producer for widget="
             << widget << " generation=" << generation;
}

QnxRenderProducer* QnxRenderProducerManager::GetProducer(
    gfx::AcceleratedWidget widget,
    uint32_t generation) {
  ProducerKey key{widget, generation};
  auto it = producers_.find(key);
  if (it == producers_.end())
    return nullptr;
  return it->second.get();
}

void QnxRenderProducerManager::RemoveAllProducers() {
  producers_.clear();
  QNX_GPU_TRACE_LOG(INFO) << "QnxRenderProducerManager: removed all producers";
}

}  // namespace ui
