// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_GL_OZONE_EGL_H_
#define UI_OZONE_PLATFORM_QNX_QNX_GL_OZONE_EGL_H_

#include <memory>

#include "base/logging.h"
#include "ui/gl/gl_display.h"
#include "ui/ozone/common/gl_ozone_egl.h"

namespace ui {

// Phase 5: GPU-side EGL/GLES2 bindings for QNX.
//
// QnxGLOzoneEGL wraps the Phase 1B-proven `eglCreateDRMImageMESA` +
// `eglExportDMABUFImageMESA` export path and provides GLES2 rendering surfaces
// for the GPU process.
//
// Key responsibilities:
// - Initialize EGL display and GLES2 bindings in the GPU process.
// - Provide offscreen GLSurface for GPU-side rendering.
// - Probe and resolve EGL_MESA_image_dma_buf_export and related function
//   pointers for QnxRenderProducer.
// - Provide a stable EGLDisplay for QnxRenderProducer.
//
// Browser-side display composition (eglCreateWindowSurface with
// screen_window_t) remains in QnxWindow; this class is GPU-process-only.
class QnxGLOzoneEGL : public GLOzoneEGL {
 public:
  QnxGLOzoneEGL();
  ~QnxGLOzoneEGL() override;

  QnxGLOzoneEGL(const QnxGLOzoneEGL&) = delete;
  QnxGLOzoneEGL& operator=(const QnxGLOzoneEGL&) = delete;

  // GLOzoneEGL:
  scoped_refptr<gl::GLSurface> CreateViewGLSurface(
      gl::GLDisplay* display,
      gfx::AcceleratedWidget widget) override;
  scoped_refptr<gl::GLSurface> CreateOffscreenGLSurface(
      gl::GLDisplay* display,
      const gfx::Size& size) override;

 protected:
  // GLOzoneEGL:
  gl::EGLDisplayPlatform GetNativeDisplay() override;
  bool LoadGLES2Bindings(
      const gl::GLImplementationParts& implementation) override;

 private:
  bool initialized_ = false;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_GL_OZONE_EGL_H_
