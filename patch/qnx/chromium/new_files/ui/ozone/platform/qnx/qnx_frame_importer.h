// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_FRAME_IMPORTER_H_
#define UI_OZONE_PLATFORM_QNX_QNX_FRAME_IMPORTER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <screen/screen.h>

#include "base/files/scoped_file.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/threading/thread_checker.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/ozone/platform/qnx/mojom/qnx_gpu.mojom.h"

namespace gl {
class GLDisplayEGL;
}  // namespace gl

namespace ui {

// Forward declaration.
class QnxWindowManager;
struct QnxWidgetRecord;

// Shortcut for the QNX mojom namespace generated from ui.ozone.qnx.mojom.
namespace qnx = ui::ozone::qnx::mojom;

// ====================================================================
// QnxFrameImporter — Browser-side DMAbuf import and Screen display
// ====================================================================
// Owned by QnxGpuHost (browser process).  Handles:
//   1. Lazy EGL display/context initialization for browser-owned Screen windows.
//   2. DMAbuf import via eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT).
//   3. GLES2 texture binding via glEGLImageTargetTexture2DOES.
//   4. GLES2 blit to Screen window surface via eglCreateWindowSurface.
//   5. eglSwapBuffers to display.
//
// Phase 5 scope (this file):
//   - Compile-safe scaffold with EGL function pointer resolution.
//   - Import of single-plane ARGB/linear DMAbuf frames.
//   - Direct eglCreateWindowSurface(screen_window_t) for browser-owned windows.
//   - Defensive handling of unsupported modifiers/plane counts.
//   - Diagnostic return values for untested runtime paths.
//
// Not in scope for this Phase 5 scaffold:
//   - Multi-plane format support beyond 1 plane (NV12, YV12, etc.).
//   - DRM modifier support (assumes linear / modifier=0).
//   - Robust GLES2 shader pipeline (uses a minimal passthrough shader).
//   - Frame-rate pacing or vsync.
//   - Full-screen damage tracking.
class QnxFrameImporter {
 public:
  // |window_manager| must outlive this object (owned by OzonePlatformQnxImpl).
  explicit QnxFrameImporter(QnxWindowManager* window_manager);
  ~QnxFrameImporter();

  QnxFrameImporter(const QnxFrameImporter&) = delete;
  QnxFrameImporter& operator=(const QnxFrameImporter&) = delete;

  // Imports a DMAbuf frame from the GPU process and displays it on the
  // browser-owned Screen window for |widget|.
  //
  // Steps:
  //   1. Look up the widget record to get screen_window_t.
  //   2. Ensure EGL display/context/surface is current for this window.
  //   3. Extract DMAbuf fds from mojo::PlatformHandle values.
  //   4. Build EGL_LINUX_DMA_BUF_EXT attribute list (single-plane ARGB).
  //   5. Call eglCreateImageKHR to import the DMAbuf as EGLImage.
  //   6. Bind EGLImage to GLES2 texture via glEGLImageTargetTexture2DOES.
  //   7. Draw fullscreen quad with the imported texture.
  //   8. Call eglSwapBuffers to display on the Screen window.
  //   9. Clean up texture and EGLImage.
  //
  // Returns: {success, diagnostic}.
  //   success=true means import and display were attempted.
  //   success=false with diagnostic "scaffold: ..." means the scaffold
  //   reached display code but hit a known untested path; the diagnostic
  //   clarifies which step deferred.
  //   An empty diagnostic on success means display was reached.
  std::pair<bool, std::string> ImportAndDisplayFrame(
      gfx::AcceleratedWidget widget,
      const qnx::QnxDmaBufFrame& frame);

  // Returns a human-readable report of EGL extensions and resolved
  // function pointers.  Useful for diagnostics.
  std::string ExtensionReport() const;

  // Returns true if EGL display was successfully initialized.
  bool IsEGLReady() const;

 private:
  // Per-window EGL state.  Lazily created when the first frame arrives
  // for a given widget.  Stored in |window_states_|.
  struct WindowEGLState {
    screen_window_t screen_win = nullptr;
    gfx::Size size;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint texture = 0;
    GLuint program = 0;
    bool valid = false;

    ~WindowEGLState();
  };

  // Resolves a single EGL or GL function pointer by name.
  // Returns nullptr if the function is not found.
  template <typename Fn>
  Fn Resolve(const char* name);

  // Initializes EGL display if not yet done.
  // Returns true on success.
  bool InitializeEGLDisplay();

  // Resolves the GL extension string (glGetString(GL_EXTENSIONS)) and the
  // glEGLImageTargetTexture2DOES function pointer. Must be called only after
  // an EGL context has been made current via eglMakeCurrent, since Mesa
  // virgl crashes when glGetString(GL_EXTENSIONS) is invoked without a
  // current context (the Khronos spec mandates NULL+error, but Mesa virgl
  // dereferences loader state).
  void EnsureGLExtensionsResolved();

  // Ensures a WindowEGLState exists for |widget| and its EGL surface
  // is current.  Lazily creates display/context/surface for the window.
  // Returns nullptr with diagnostic on failure.
  WindowEGLState* GetOrCreateWindowState(gfx::AcceleratedWidget widget,
                                         const QnxWidgetRecord* record,
                                         const qnx::QnxDmaBufFrame& frame,
                                         std::string* out_diagnostic);

  // Builds the EGL_LINUX_DMA_BUF_EXT attribute list for single-plane ARGB
  // frames.  |frame| provides width/height/fourcc/modifier.
  // |fds| must have at least one element (the plane fd).
  // Fills |attrs| with EGL attribute list terminated by EGL_NONE.
  // Returns true on success.
  bool BuildDmaBufAttrs(const qnx::QnxDmaBufFrame& frame,
                        const std::vector<base::ScopedFD>& fds,
                        std::vector<EGLint>* attrs);

  // Imports a DMAbuf frame as an EGLImage and binds it to |texture|.
  // |attrs| must be a valid EGL_LINUX_DMA_BUF_EXT attribute list.
  // Returns the EGLImage or EGL_NO_IMAGE_KHR on failure.
  EGLImageKHR ImportDmaBufToTexture(
      EGLDisplay display,
      const std::vector<EGLint>& attrs,
      GLuint texture);

  // Compiles and links a minimal GLES2 shader program for fullscreen quad
  // texturing.  Returns the program id or 0 on failure.
  GLuint CompileShaderProgram();

  // Draws a fullscreen quad with |texture| using |program|.
  void DrawFullscreenQuad(GLuint program, GLuint texture);

  // Cleans up OpenGL resources for |state|.
  void CleanupGLResources(WindowEGLState* state);

  // Non-owning pointer to the widget record table.
  raw_ptr<QnxWindowManager> window_manager_;

  // True if EGL display has been initialized (lazy, one-time).
  bool egl_initialized_ = false;

  // True if the minimum EGL/GL extensions for DMAbuf import are present.
  bool can_import_dma_buf_ = false;

  // True once EnsureGLExtensionsResolved() has run successfully. The
  // glGetString + glEGLImageTargetTexture2DOES resolution is deferred to
  // the first eglMakeCurrent in GetOrCreateWindowState, not the EGL
  // display init.
  bool gl_extensions_resolved_ = false;

  // EGL display used for all browser-side EGL operations.
  // Initialized once in InitializeEGLDisplay().
  EGLDisplay egl_display_ = EGL_NO_DISPLAY;

  // ---- EGL function pointers (DMAbuf import) ----

  using EglCreateImageKHRFn = EGLImageKHR(
      EGLDisplay dpy,
      EGLContext ctx,
      EGLenum target,
      EGLClientBuffer buffer,
      const EGLint* attrib_list);
  EglCreateImageKHRFn* egl_create_image_khr_ = nullptr;

  using EglDestroyImageKHRFn =
      EGLBoolean(EGLDisplay dpy, EGLImageKHR image);
  EglDestroyImageKHRFn* egl_destroy_image_khr_ = nullptr;

  // ---- GL function pointers (EGLImage binding) ----

  using GlEGLImageTargetTexture2DOESFn =
      void(GLenum target, void* image);
  GlEGLImageTargetTexture2DOESFn* gl_egl_image_target_texture_2d_oes_ = nullptr;

  // ---- Extension presence flags ----

  bool has_egl_ext_image_dma_buf_import_ = false;
  bool has_gl_oes_egl_image_ = false;
  bool has_khr_gl_texture_2d_ = false;

  // Per-window EGL state, keyed by widget.
  // Created lazily when the first frame arrives for a widget.
  std::map<gfx::AcceleratedWidget, std::unique_ptr<WindowEGLState>>
      window_states_;

  // ---- GLES2 shader sources ----

  static constexpr char kVertexShaderSrc[] = R"(
      attribute vec4 a_position;
      attribute vec2 a_texCoord;
      varying vec2 v_texCoord;
      void main() {
        gl_Position = a_position;
        v_texCoord = a_texCoord;
      }
    )";

  static constexpr char kFragmentShaderSrc[] = R"(
      precision mediump float;
      uniform sampler2D u_texture;
      varying vec2 v_texCoord;
      void main() {
        gl_FragColor = texture2D(u_texture, v_texCoord);
      }
    )";

  // Thread check: all methods must be called from the browser UI thread.
  base::ThreadChecker thread_checker_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_FRAME_IMPORTER_H_
