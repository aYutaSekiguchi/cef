// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 5: Browser-side EGL/Screen DMAbuf import and display scaffold.
// See docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md.
//
// This file implements browser-side DMAbuf import using the Phase 1B-proven
// path:
//
//   GPU process: eglCreateDRMImageMESA → eglExportDMABUFImageMESA → fd
//                ↓ SCM_RIGHTS via Mojo handle<platform>
//   Browser: mojo::PlatformHandle::TakeFD() → eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)
//            → glEGLImageTargetTexture2DOES → GLES2 texture
//            → eglCreateWindowSurface(screen_window_t) → draw → eglSwapBuffers
//
// Phase 5 scope (this file):
//   - Import of single-plane ARGB/linear DMAbuf frames.
//   - Direct eglCreateWindowSurface(screen_window_t) for browser-owned windows.
//   - Minimal GLES2 shader pipeline for fullscreen quad display.
//   - Defensive handling of unsupported modifiers/plane counts.
//   - Diagnostic return values for untested runtime paths.
//
// Not in scope:
//   - Multi-plane format support (NV12, YV12, etc.).
//   - DRM modifier support (assumes linear / modifier=0).
//   - Full-screen damage tracking or frame-rate pacing.

#include "ui/ozone/platform/qnx/qnx_frame_importer.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

#include <dlfcn.h>

#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

#include "base/logging.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"

// EGL extension function pointer typedefs.
typedef EGLImageKHR(EGLAPIENTRYP PFNEGLCREATEIMAGEKHRPROC)(
    EGLDisplay dpy,
    EGLContext ctx,
    EGLenum target,
    EGLClientBuffer buffer,
    const EGLint* attrib_list);
typedef EGLBoolean(EGLAPIENTRYP PFNEGLDESTROYIMAGEKHRPROC)(
    EGLDisplay dpy, EGLImageKHR image);

// GL extension function pointer typedefs.
typedef void(EGLAPIENTRYP PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(
    GLenum target, void* image);

namespace ui {
namespace qnx = ui::ozone::qnx::mojom;

namespace {

// Returns a human-readable name for an EGL error code.
const char* EglErrorName(EGLint err) {
  switch (err) {
    case EGL_SUCCESS:
      return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
      return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ALLOC:
      return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
      return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:
      return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:
      return "EGL_BAD_CONTEXT";
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

// Checks a GL error and logs it.  Returns true if no error.
bool CheckGL(const char* expr, const char* file, int line) {
  GLenum err = glGetError();
  if (err != GL_NO_ERROR) {
    LOG(ERROR) << "GL error at " << file << ":" << line
               << " after " << expr << ": " << GlErrorName(err)
               << " (0x" << std::hex << err << std::dec << ")";
    return false;
  }
  return true;
}

// Checks an EGL error and logs it.  Returns true if no error.
bool CheckEGL(const char* expr, EGLDisplay display, const char* file, int line) {
  EGLint err = eglGetError();
  if (err != EGL_SUCCESS) {
    LOG(ERROR) << "EGL error at " << file << ":" << line
               << " after " << expr << ": " << EglErrorName(err)
               << " (0x" << std::hex << err << std::dec << ")";
    return false;
  }
  return true;
}

// Macro to wrap GL/EGL error checks.
#define CHECK_GLEXR(expr) CheckGL(#expr, __FILE__, __LINE__)
#define CHECK_EGLERR(expr, display) CheckEGL(#expr, display, __FILE__, __LINE__)

}  // namespace

// ======================================================================
// QnxFrameImporter::WindowEGLState
// ======================================================================

QnxFrameImporter::WindowEGLState::~WindowEGLState() {
  // Destroy in reverse order of creation.
  if (context != EGL_NO_CONTEXT) {
    if (display != EGL_NO_DISPLAY) {
      eglDestroyContext(display, context);
      QNX_GPU_TRACE_LOG(INFO) << "~WindowEGLState: eglDestroyContext done";
    }
  }
  if (surface != EGL_NO_SURFACE) {
    if (display != EGL_NO_DISPLAY) {
      eglDestroySurface(display, surface);
      QNX_GPU_TRACE_LOG(INFO) << "~WindowEGLState: eglDestroySurface done";
    }
  }
  if (texture != 0) {
    glDeleteTextures(1, &texture);
    QNX_GPU_TRACE_LOG(INFO) << "~WindowEGLState: glDeleteTextures done";
  }
  if (program != 0) {
    glDeleteProgram(program);
    QNX_GPU_TRACE_LOG(INFO) << "~WindowEGLState: glDeleteProgram done";
  }
}

// ======================================================================
// QnxFrameImporter
// ======================================================================

QnxFrameImporter::QnxFrameImporter(QnxWindowManager* window_manager)
    : window_manager_(window_manager) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter: constructed";
}

QnxFrameImporter::~QnxFrameImporter() {
  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter: destroyed; cleaning up window states";
  window_states_.clear();
}

std::string QnxFrameImporter::ExtensionReport() const {
  std::ostringstream oss;
  oss << "QnxFrameImporter extension report:\n";
  oss << "  egl_display: "
      << static_cast<void*>(egl_display_) << "\n";
  oss << "  EGL_EXT_image_dma_buf_import: "
      << (has_egl_ext_image_dma_buf_import_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  GL_OES_EGL_image:               "
      << (has_gl_oes_egl_image_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  EGL_KHR_gl_texture_2d:          "
      << (has_khr_gl_texture_2d_ ? "PRESENT" : "ABSENT") << "\n";
  oss << "  eglCreateImageKHR:              "
      << (egl_create_image_khr_ ? "RESOLVED" : "NOT RESOLVED") << "\n";
  oss << "  eglDestroyImageKHR:             "
      << (egl_destroy_image_khr_ ? "RESOLVED" : "NOT RESOLVED") << "\n";
  oss << "  glEGLImageTargetTexture2DOES:   "
      << (gl_egl_image_target_texture_2d_oes_ ? "RESOLVED" : "NOT RESOLVED") << "\n";
  return oss.str();
}

bool QnxFrameImporter::IsEGLReady() const {
  return egl_initialized_ && egl_display_ != EGL_NO_DISPLAY;
}

// ======================================================================
// EGL display initialization
// ======================================================================

bool QnxFrameImporter::InitializeEGLDisplay() {
  if (egl_initialized_) {
    return egl_display_ != EGL_NO_DISPLAY;
  }

  egl_initialized_ = true;

  // ---- Get EGL display ----
  // In the browser process, we use EGL_DEFAULT_DISPLAY.
  // The browser process has a QNX Screen context but no dedicated EGL display.
  // Using EGL_DEFAULT_DISPLAY is the Phase 1B-display-proven approach.
  egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (egl_display_ == EGL_NO_DISPLAY) {
    LOG(ERROR) << "QnxFrameImporter: eglGetDisplay(EGL_DEFAULT_DISPLAY) "
                  "returned EGL_NO_DISPLAY";
    return false;
  }

  // Initialize EGL.
  EGLint maj = 0, min = 0;
  if (!eglInitialize(egl_display_, &maj, &min)) {
    LOG(ERROR) << "QnxFrameImporter: eglInitialize failed: "
                << EglErrorName(eglGetError());
    egl_display_ = EGL_NO_DISPLAY;
    return false;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter: EGL " << maj << "." << min
             << " initialized (display=" << static_cast<void*>(egl_display_)
             << ")";

  // ---- Probe EGL extensions ----
  const char* egl_exts = eglQueryString(egl_display_, EGL_EXTENSIONS);
  if (egl_exts) {
    QNX_GPU_TRACE_LOG(INFO) << "EGL_EXTENSIONS (" << strlen(egl_exts)
               << " chars): " << egl_exts;
  } else {
    QNX_GPU_TRACE_LOG(INFO) << "EGL_EXTENSIONS: eglQueryString returned null";
  }

  has_egl_ext_image_dma_buf_import_ =
      egl_exts && strstr(egl_exts, "EGL_EXT_image_dma_buf_import") != nullptr;

  // ---- Resolve EGL function pointers ----
  egl_create_image_khr_ = Resolve<EglCreateImageKHRFn*>("eglCreateImageKHR");
  egl_destroy_image_khr_ =
      Resolve<EglDestroyImageKHRFn*>("eglDestroyImageKHR");

  // NOTE: glGetString(GL_EXTENSIONS) and glEGLImageTargetTexture2DOES
  // resolution are intentionally deferred to EnsureGLExtensionsResolved()
  // which runs after the first eglMakeCurrent in GetOrCreateWindowState.
  // Calling glGetString here would crash Mesa virgl because no GL context
  // is current at this point.
  can_import_dma_buf_ = has_egl_ext_image_dma_buf_import_ &&
                         egl_create_image_khr_ != nullptr &&
                         egl_destroy_image_khr_ != nullptr;
  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter: EGL display initialized; "
                "GL extension resolution deferred until first eglMakeCurrent";
  return true;
}

void QnxFrameImporter::EnsureGLExtensionsResolved() {
  if (gl_extensions_resolved_) {
    return;
  }
  // Safe to call glGetString now: caller has just made an EGL context
  // current via eglMakeCurrent. The Khronos spec says glGetString must
  // return NULL + GL_INVALID_OPERATION when no context is current, but
  // Mesa virgl dereferences loader state and segfaults.
  const char* gl_exts =
      reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  if (gl_exts) {
    QNX_GPU_TRACE_LOG(INFO) << "GL_EXTENSIONS: " << gl_exts;
  } else {
    QNX_GPU_TRACE_LOG(INFO) << "GL_EXTENSIONS: glGetString returned null";
  }
  has_gl_oes_egl_image_ =
      gl_exts && strstr(gl_exts, "GL_OES_EGL_image") != nullptr;
  has_khr_gl_texture_2d_ =
      gl_exts && strstr(gl_exts, "GL_KHR_gl_texture_2d") != nullptr;
  gl_egl_image_target_texture_2d_oes_ =
      Resolve<GlEGLImageTargetTexture2DOESFn*>("glEGLImageTargetTexture2DOES");
  gl_extensions_resolved_ = true;
  // Re-evaluate the import capability now that GL extensions are known.
  if (!can_import_dma_buf_ && has_egl_ext_image_dma_buf_import_ &&
      has_gl_oes_egl_image_ && egl_create_image_khr_ != nullptr &&
      egl_destroy_image_khr_ != nullptr &&
      gl_egl_image_target_texture_2d_oes_ != nullptr) {
    can_import_dma_buf_ = true;
  }
  if (can_import_dma_buf_) {
    QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter: GL extensions resolved; "
                  "EGL display ready for DMAbuf import";
  } else {
    DLOG(WARNING) << "QnxFrameImporter: GL extensions resolved but "
                     "DMAbuf import still incomplete:\n"
                  << ExtensionReport();
  }
}

template <typename Fn>
Fn QnxFrameImporter::Resolve(const char* name) {
  auto proc = reinterpret_cast<__eglMustCastToProperFunctionPointerType>(
      eglGetProcAddress(name));
  if (!proc) {
    DLOG(WARNING) << "QnxFrameImporter: eglGetProcAddress(\"" << name
                  << "\") returned null";
    return nullptr;
  }
  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter: resolved " << name;
  return reinterpret_cast<Fn>(proc);
}

// ======================================================================
// Per-window EGL state management
// ======================================================================

QnxFrameImporter::WindowEGLState*
QnxFrameImporter::GetOrCreateWindowState(
    gfx::AcceleratedWidget widget,
    const QnxWidgetRecord* record,
    const qnx::QnxDmaBufFrame& frame,
    std::string* out_diagnostic) {
  out_diagnostic->clear();

  // Check if we already have state for this widget.
  auto it = window_states_.find(widget);
  if (it != window_states_.end()) {
    // State exists.  Make its surface current.
    WindowEGLState* state = it->second.get();
    if (state->valid && state->display != EGL_NO_DISPLAY) {
      if (!eglMakeCurrent(state->display, state->surface, state->surface,
                          state->context)) {
        *out_diagnostic = "scaffold: eglMakeCurrent failed for existing "
                         "window state; import/display deferred: " +
                         std::string(EglErrorName(eglGetError()));
        DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: "
                       "eglMakeCurrent failed: " << EglErrorName(eglGetError());
        return nullptr;
      }
      QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::GetOrCreateWindowState: reusing "
                    "existing state for widget="
                 << widget;
      return state;
    }
  }

  // ---- Need to create new window EGL state ----

  // Lazily initialize EGL display if not yet done.
  if (!InitializeEGLDisplay()) {
    *out_diagnostic = "scaffold: EGL display initialization failed; "
                      "import/display deferred";
    DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: EGL display "
                   "not ready";
    return nullptr;
  }

  // Get screen_window_t from the widget record.
  screen_window_t screen_win =
      record ? static_cast<screen_window_t>(record->screen_win) : nullptr;
  if (!screen_win) {
    *out_diagnostic = "scaffold: no screen_window_t for widget; "
                      "import/display deferred";
    DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: "
                   "no screen_window_t for widget="
                << widget;
    return nullptr;
  }

  // ---- Step 1: Choose EGL config for window surface ----
  // Request a config compatible with SCREEN_USAGE_OPENGL_ES2 windows.
  // Matches the Phase 1B-display-isolation probe config selection.
  const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_NONE};

  EGLint num_configs = 0;
  if (!eglChooseConfig(egl_display_, config_attribs, nullptr, 0,
                       &num_configs)) {
    *out_diagnostic = "scaffold: eglChooseConfig failed; "
                      "import/display deferred: " +
                      std::string(EglErrorName(eglGetError()));
    DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: "
                   "eglChooseConfig (count) failed: "
                << EglErrorName(eglGetError());
    return nullptr;
  }

  if (num_configs == 0) {
    *out_diagnostic = "scaffold: no EGL configs found for window surface; "
                      "import/display deferred";
    DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: "
                   "no EGL configs found";
    return nullptr;
  }

  EGLConfig config;
  if (!eglChooseConfig(egl_display_, config_attribs, &config, 1,
                       &num_configs)) {
    *out_diagnostic = "scaffold: eglChooseConfig (select) failed; "
                      "import/display deferred: " +
                      std::string(EglErrorName(eglGetError()));
    DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: "
                   "eglChooseConfig (select) failed: "
                << EglErrorName(eglGetError());
    return nullptr;
  }

  // ---- Step 2: Create EGL window surface ----
  // Phase 1B-display-isolation proved: direct eglCreateWindowSurface with
  // screen_window_t succeeds without SCREEN_PROPERTY_EGL_HANDLE.
  // Cast screen_window_t to EGLNativeWindowType.
  EGLSurface surface = eglCreateWindowSurface(
      egl_display_, config,
      reinterpret_cast<EGLNativeWindowType>(screen_win),
      nullptr);
  if (surface == EGL_NO_SURFACE) {
    *out_diagnostic = "scaffold: eglCreateWindowSurface(screen_win) failed; "
                      "import/display deferred: " +
                      std::string(EglErrorName(eglGetError()));
    DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: "
                   "eglCreateWindowSurface failed: "
                << EglErrorName(eglGetError());
    return nullptr;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::GetOrCreateWindowState: "
                 "eglCreateWindowSurface OK (surface="
              << static_cast<void*>(surface) << ")";

  // ---- Step 3: Create EGL context (GLES2) ----
  const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE};
  EGLContext context = eglCreateContext(
      egl_display_, config, EGL_NO_CONTEXT, context_attribs);
  if (context == EGL_NO_CONTEXT) {
    *out_diagnostic = "scaffold: eglCreateContext failed; "
                      "import/display deferred: " +
                      std::string(EglErrorName(eglGetError()));
    DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: "
                   "eglCreateContext failed: "
                << EglErrorName(eglGetError());
    eglDestroySurface(egl_display_, surface);
    return nullptr;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::GetOrCreateWindowState: "
                 "eglCreateContext OK (context="
              << static_cast<void*>(context) << ")";

  // ---- Step 4: Make context current ----
  if (!eglMakeCurrent(egl_display_, surface, surface, context)) {
    *out_diagnostic = "scaffold: eglMakeCurrent failed; "
                      "import/display deferred: " +
                      std::string(EglErrorName(eglGetError()));
    DLOG(ERROR) << "QnxFrameImporter::GetOrCreateWindowState: "
                   "eglMakeCurrent failed: "
                << EglErrorName(eglGetError());
    eglDestroyContext(egl_display_, context);
    eglDestroySurface(egl_display_, surface);
    return nullptr;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::GetOrCreateWindowState: "
                "eglMakeCurrent OK";

  // ---- Step 4b: Resolve GL extensions now that a context is current ----
  // Safe to call glGetString(GL_EXTENSIONS) and resolve
  // glEGLImageTargetTexture2DOES only after eglMakeCurrent; calling them
  // earlier (in InitializeEGLDisplay) crashes Mesa virgl.
  EnsureGLExtensionsResolved();

  // ---- Step 5: Create GLES2 texture ----
  GLuint texture = 0;
  glGenTextures(1, &texture);
  if (texture == 0 || !CHECK_GLEXR(glGenTextures)) {
    *out_diagnostic = "scaffold: glGenTextures failed; "
                      "import/display deferred";
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    eglDestroyContext(egl_display_, context);
    eglDestroySurface(egl_display_, surface);
    return nullptr;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  // Set texture parameters for non-mipmapped texture.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  CHECK_GLEXR(glBindTexture);

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::GetOrCreateWindowState: "
                 "GL texture created: "
              << texture;

  // ---- Step 6: Compile GLES2 shader program ----
  GLuint program = CompileShaderProgram();
  if (program == 0) {
    *out_diagnostic = "scaffold: shader compilation failed; "
                      "import/display deferred";
    glDeleteTextures(1, &texture);
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    eglDestroyContext(egl_display_, context);
    eglDestroySurface(egl_display_, surface);
    return nullptr;
  }

  // ---- Store the state ----
  auto state = std::make_unique<WindowEGLState>();
  state->screen_win = screen_win;
  state->size = gfx::Size(static_cast<int>(frame.width),
                           static_cast<int>(frame.height));
  state->display = egl_display_;
  state->config = config;
  state->surface = surface;
  state->context = context;
  state->texture = texture;
  state->program = program;
  state->valid = true;

  WindowEGLState* raw = state.get();
  window_states_.emplace(widget, std::move(state));

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::GetOrCreateWindowState: created state "
                 "for widget="
              << widget << " surface=" << static_cast<void*>(surface)
              << " texture=" << texture << " program=" << program;

  return raw;
}

// ======================================================================
// Shader compilation
// ======================================================================

GLuint QnxFrameImporter::CompileShaderProgram() {
  // ---- Compile vertex shader ----
  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  if (vs == 0) {
    DLOG(ERROR) << "QnxFrameImporter::CompileShaderProgram: "
                   "glCreateShader(vertex) failed";
    return 0;
  }
  const char* vs_src = kVertexShaderSrc;
  glShaderSource(vs, 1, &vs_src, nullptr);
  glCompileShader(vs);

  GLint compiled = 0;
  glGetShaderiv(vs, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint len = 0;
    glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &len);
    std::string log(len + 1, '\0');
    glGetShaderInfoLog(vs, len, nullptr, &log[0]);
    DLOG(ERROR) << "QnxFrameImporter::CompileShaderProgram: "
                   "vertex shader compilation failed:\n"
                << log;
    glDeleteShader(vs);
    return 0;
  }

  // ---- Compile fragment shader ----
  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  if (fs == 0) {
    DLOG(ERROR) << "QnxFrameImporter::CompileShaderProgram: "
                   "glCreateShader(fragment) failed";
    glDeleteShader(vs);
    return 0;
  }
  const char* fs_src = kFragmentShaderSrc;
  glShaderSource(fs, 1, &fs_src, nullptr);
  glCompileShader(fs);

  compiled = 0;
  glGetShaderiv(fs, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint len = 0;
    glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &len);
    std::string log(len + 1, '\0');
    glGetShaderInfoLog(fs, len, nullptr, &log[0]);
    DLOG(ERROR) << "QnxFrameImporter::CompileShaderProgram: "
                   "fragment shader compilation failed:\n"
                << log;
    glDeleteShader(fs);
    glDeleteShader(vs);
    return 0;
  }

  // ---- Link program ----
  GLuint program = glCreateProgram();
  if (program == 0) {
    DLOG(ERROR) << "QnxFrameImporter::CompileShaderProgram: "
                   "glCreateProgram failed";
    glDeleteShader(fs);
    glDeleteShader(vs);
    return 0;
  }

  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);

  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint len = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
    std::string log(len + 1, '\0');
    glGetProgramInfoLog(program, len, nullptr, &log[0]);
    DLOG(ERROR) << "QnxFrameImporter::CompileShaderProgram: "
                   "program link failed:\n"
                << log;
    glDeleteProgram(program);
    glDeleteShader(fs);
    glDeleteShader(vs);
    return 0;
  }

  // Shaders can be deleted after linking; they remain in the program.
  glDeleteShader(vs);
  glDeleteShader(fs);

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::CompileShaderProgram: "
                "program="
             << program << " linked OK";
  return program;
}

// ======================================================================
// DMAbuf attribute list construction
// ======================================================================

bool QnxFrameImporter::BuildDmaBufAttrs(
    const qnx::QnxDmaBufFrame& frame,
    const std::vector<base::ScopedFD>& fds,
    std::vector<EGLint>* attrs) {
  attrs->clear();

  // ---- Required attributes ----
  attrs->push_back(EGL_WIDTH);
  attrs->push_back(static_cast<EGLint>(frame.width));
  attrs->push_back(EGL_HEIGHT);
  attrs->push_back(static_cast<EGLint>(frame.height));
  attrs->push_back(EGL_LINUX_DRM_FOURCC_EXT);
  attrs->push_back(static_cast<EGLint>(frame.fourcc));

  // ---- Plane 0 (single-plane formats only in this scaffold) ----
  if (fds.empty()) {
    DLOG(ERROR) << "QnxFrameImporter::BuildDmaBufAttrs: no fds provided";
    return false;
  }

  if (frame.planes.size() != 1) {
    // Scaffold limitation: multi-plane formats not implemented.
    // Return diagnostic indicating the step that deferred.
    DLOG(WARNING) << "QnxFrameImporter::BuildDmaBufAttrs: "
                     "multi-plane format (planes="
                  << frame.planes.size()
                  << ") not supported by scaffold; import deferred";
    return false;
  }

  // Use the raw fd value for the EGL attribute.
  // The ScopedFD is moved into fds and the raw fd is used here;
  // ownership is NOT transferred to EGL (EGL does not own fds).
  int raw_fd = fds[0].get();

  attrs->push_back(EGL_DMA_BUF_PLANE0_FD_EXT);
  attrs->push_back(raw_fd);
  attrs->push_back(EGL_DMA_BUF_PLANE0_OFFSET_EXT);
  attrs->push_back(static_cast<EGLint>(frame.planes[0]->offset));
  attrs->push_back(EGL_DMA_BUF_PLANE0_PITCH_EXT);
  attrs->push_back(static_cast<EGLint>(frame.planes[0]->stride));

  // ---- Modifier ----
  // Phase 1B probes showed modifier=0 (linear) works.  Non-zero modifiers
  // require EGL_EXT_image_dma_buf_import_modifiers which is not guaranteed.
  if (frame.modifier != 0) {
    DLOG(WARNING) << "QnxFrameImporter::BuildDmaBufAttrs: non-linear "
                     "modifier=0x"
                  << std::hex << frame.modifier << std::dec
                  << "; import deferred (modifier support not scaffolded)";
    return false;
  }

  // ---- Terminator ----
  attrs->push_back(EGL_NONE);

  return true;
}

// ======================================================================
// EGLImage import
// ======================================================================

EGLImageKHR QnxFrameImporter::ImportDmaBufToTexture(
    EGLDisplay display,
    const std::vector<EGLint>& attrs,
    GLuint texture) {
  if (!egl_create_image_khr_) {
    DLOG(ERROR) << "QnxFrameImporter::ImportDmaBufToTexture: "
                   "eglCreateImageKHR not resolved";
    return EGL_NO_IMAGE_KHR;
  }

  // Import DMAbuf as EGLImage.  EGL_NO_CONTEXT is correct for
  // EGL_LINUX_DMA_BUF_EXT (no client buffer).
  EGLImageKHR egl_image = egl_create_image_khr_(
      display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
      static_cast<EGLClientBuffer>(nullptr), attrs.data());

  if (egl_image == EGL_NO_IMAGE_KHR) {
    DLOG(ERROR) << "QnxFrameImporter::ImportDmaBufToTexture: "
                   "eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT) failed: "
                << EglErrorName(eglGetError());
    return EGL_NO_IMAGE_KHR;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::ImportDmaBufToTexture: "
                "EGLImage created: "
             << static_cast<void*>(egl_image);

  // Bind EGLImage to GL texture.
  if (!gl_egl_image_target_texture_2d_oes_) {
    DLOG(ERROR) << "QnxFrameImporter::ImportDmaBufToTexture: "
                   "glEGLImageTargetTexture2DOES not resolved";
    if (egl_destroy_image_khr_) {
      egl_destroy_image_khr_(display, egl_image);
    }
    return EGL_NO_IMAGE_KHR;
  }

  glBindTexture(GL_TEXTURE_2D, texture);
  gl_egl_image_target_texture_2d_oes_(GL_TEXTURE_2D, egl_image);
  if (!CHECK_GLEXR(glEGLImageTargetTexture2DOES)) {
    DLOG(ERROR) << "QnxFrameImporter::ImportDmaBufToTexture: "
                   "glEGLImageTargetTexture2DOES failed";
    if (egl_destroy_image_khr_) {
      egl_destroy_image_khr_(display, egl_image);
    }
    return EGL_NO_IMAGE_KHR;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::ImportDmaBufToTexture: "
                "DMAbuf imported to texture="
             << texture;
  return egl_image;
}

// ======================================================================
// Fullscreen quad draw
// ======================================================================

void QnxFrameImporter::DrawFullscreenQuad(GLuint program, GLuint texture) {
  glUseProgram(program);

  // Set texture sampler.
  GLint u_tex = glGetUniformLocation(program, "u_texture");
  glUniform1i(u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);

  // Fullscreen quad: 2 triangles covering NDC [-1,1].
  // Vertex positions and texcoords interleaved: {x, y, u, v}.
  static const GLfloat kQuadData[] = {
      // Triangle 1
      -1.0f, -1.0f, 0.0f, 0.0f,  // bottom-left
       1.0f, -1.0f, 1.0f, 0.0f,  // bottom-right
      -1.0f,  1.0f, 0.0f, 1.0f,  // top-left
      // Triangle 2
       1.0f, -1.0f, 1.0f, 0.0f,  // bottom-right
       1.0f,  1.0f, 1.0f, 1.0f,  // top-right
      -1.0f,  1.0f, 0.0f, 1.0f,  // top-left
  };

  GLint a_position = glGetAttribLocation(program, "a_position");
  GLint a_texCoord = glGetAttribLocation(program, "a_texCoord");

  glVertexAttribPointer(a_position, 2, GL_FLOAT, GL_FALSE,
                        4 * sizeof(GLfloat), kQuadData);
  glVertexAttribPointer(a_texCoord, 2, GL_FLOAT, GL_FALSE,
                        4 * sizeof(GLfloat), kQuadData + 2);

  glEnableVertexAttribArray(a_position);
  glEnableVertexAttribArray(a_texCoord);

  glDrawArrays(GL_TRIANGLES, 0, 6);

  glDisableVertexAttribArray(a_position);
  glDisableVertexAttribArray(a_texCoord);

  glUseProgram(0);
  CHECK_GLEXR(glDrawArrays);
}

// ======================================================================
// GL resource cleanup
// ======================================================================

void QnxFrameImporter::CleanupGLResources(WindowEGLState* state) {
  if (!state)
    return;
  if (state->texture != 0) {
    glDeleteTextures(1, &state->texture);
    state->texture = 0;
    QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::CleanupGLResources: "
                  "texture deleted";
  }
  if (state->program != 0) {
    glDeleteProgram(state->program);
    state->program = 0;
    QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::CleanupGLResources: "
                  "program deleted";
  }
}

// ======================================================================
// ImportAndDisplayFrame — main entry point
// ======================================================================

std::pair<bool, std::string> QnxFrameImporter::ImportAndDisplayFrame(
    gfx::AcceleratedWidget widget,
    const qnx::QnxDmaBufFrame& frame) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::ImportAndDisplayFrame: widget="
             << widget << " size=" << frame.width << "x" << frame.height
             << " fourcc=0x" << std::hex << frame.fourcc
             << " (" << FourccToString(frame.fourcc) << ")"
             << " modifier=0x" << frame.modifier << std::dec
             << " planes=" << frame.planes.size();

  // ---- Get widget record ----
  const QnxWidgetRecord* record = nullptr;
  if (window_manager_) {
    record = window_manager_->GetWidgetRecord(widget);
  }
  if (!record) {
    return {false, "scaffold: no widget record for widget; "
                    "import/display deferred"};
  }

  // ---- Get or create per-window EGL state ----
  std::string diag;
  WindowEGLState* state =
      GetOrCreateWindowState(widget, record, frame, &diag);
  if (!state) {
    DLOG(ERROR) << "QnxFrameImporter::ImportAndDisplayFrame: "
                   "GetOrCreateWindowState failed: " << diag;
    return {false, diag};
  }

  if (!state->valid || state->display == EGL_NO_DISPLAY) {
    return {false, "scaffold: window EGL state invalid; "
                    "import/display deferred"};
  }

  // ---- Extract DMAbuf fds from Mojo PlatformHandle values ----
  //
  // Mojo IPC receives the DMAbuf fd via SCM_RIGHTS and wraps it in
  // mojo::PlatformHandle.  We must take ownership of the fd using
  // PlatformHandle::TakeFD() to avoid leaking it.
  //
  // NOTE: The Mojo handle is valid for the lifetime of the mojom call.
  // After the Mojo callback completes, the handle may be invalid.
  // We must extract the fd before returning from this call.
  if (frame.planes.empty()) {
    return {false, "scaffold: frame has no planes; import/display deferred"};
  }

  std::vector<base::ScopedFD> scoped_fds;
  scoped_fds.reserve(frame.planes.size());
  for (size_t i = 0; i < frame.planes.size(); ++i) {
    const auto& plane = frame.planes[i];

    // Take ownership of the fd from the Mojo PlatformHandle.
    // TakeFD() returns a valid fd and makes the PlatformHandle invalid.
    // If the fd is already invalid, TakeFD() returns -1.
    base::ScopedFD fd = plane->fd.TakeFD();
    if (!fd.is_valid()) {
      DLOG(WARNING) << "QnxFrameImporter::ImportAndDisplayFrame: "
                       "plane "
                    << i << " has invalid fd; skipping";
      continue;
    }
    scoped_fds.push_back(std::move(fd));
  }

  if (scoped_fds.empty()) {
    return {false, "scaffold: no valid DMAbuf fds; import/display deferred"};
  }

  // ---- Build EGL_LINUX_DMA_BUF_EXT attribute list ----
  std::vector<EGLint> attrs;
  if (!BuildDmaBufAttrs(frame, scoped_fds, &attrs)) {
    return {false, "scaffold: BuildDmaBufAttrs failed (multi-plane or "
                    "non-linear modifier); import/display deferred"};
  }

  // ---- Import DMAbuf to GL texture ----
  // The EGLImage takes a reference to the DMAbuf fd; the fd remains
  // valid for the lifetime of the EGLImage.  The ScopedFD in scoped_fds
  // will close the fd when it goes out of scope, which may or may not
  // be after the EGLImage is destroyed.  This is safe because:
  //   1. EGL takes a dup() of the fd at eglCreateImageKHR time.
  //   2. The scoped fd closing does not affect EGL's dup.
  //   3. The EGLImage is destroyed via eglDestroyImageKHR below.
  EGLImageKHR egl_image = ImportDmaBufToTexture(
      state->display, attrs, state->texture);

  if (egl_image == EGL_NO_IMAGE_KHR) {
    return {false, "scaffold: eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT) "
                    "failed; import/display deferred"};
  }

  // ---- Draw fullscreen quad with imported texture ----
  DrawFullscreenQuad(state->program, state->texture);
  CHECK_GLEXR(glDrawArrays);

  // ---- Swap buffers to display on Screen window ----
  // eglSwapBuffers posts the rendered frame to the Screen window surface.
  if (!eglSwapBuffers(state->display, state->surface)) {
    DLOG(ERROR) << "QnxFrameImporter::ImportAndDisplayFrame: "
                   "eglSwapBuffers failed: "
                << EglErrorName(eglGetError());
    if (egl_destroy_image_khr_) {
      egl_destroy_image_khr_(state->display, egl_image);
    }
    return {false, "scaffold: eglSwapBuffers failed; display deferred"};
  }

  // ---- Destroy EGLImage ----
  // The EGLImage is no longer needed after the frame is displayed.
  // EGL takes its own reference to the DMAbuf at import time.
  if (egl_destroy_image_khr_) {
    EGLBoolean destroyed = egl_destroy_image_khr_(state->display, egl_image);
    if (!destroyed) {
      DLOG(WARNING) << "QnxFrameImporter::ImportAndDisplayFrame: "
                       "eglDestroyImageKHR failed: "
                    << EglErrorName(eglGetError());
      // Non-fatal: EGL will clean up on display destruction.
    } else {
      QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::ImportAndDisplayFrame: "
                    "EGLImage destroyed";
    }
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxFrameImporter::ImportAndDisplayFrame: widget="
             << widget << " size=" << frame.width << "x" << frame.height
             << " displayed on Screen window; "
                "import/display scaffold reached display code";

  return {true, std::string()};
}

}  // namespace ui
