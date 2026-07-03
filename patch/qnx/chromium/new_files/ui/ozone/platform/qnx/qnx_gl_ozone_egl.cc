// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 5: GPU-side EGL/GLES2 bindings for QNX.
// See docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md.

#include "ui/ozone/platform/qnx/qnx_gl_ozone_egl.h"

#include <memory>

#include "base/check.h"
#include "base/logging.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/ozone/common/egl_util.h"

namespace ui {

QnxGLOzoneEGL::QnxGLOzoneEGL() = default;

QnxGLOzoneEGL::~QnxGLOzoneEGL() = default;

gl::EGLDisplayPlatform QnxGLOzoneEGL::GetNativeDisplay() {
  // Phase 1B-display proved that eglGetDisplay(EGL_DEFAULT_DISPLAY) maps to
  // the correct Mesa/virgl EGL display in the GPU process.
  // On real QNX hardware this also maps to the QNX GPU driver EGL display.
  return gl::EGLDisplayPlatform(EGL_DEFAULT_DISPLAY);
}

bool QnxGLOzoneEGL::LoadGLES2Bindings(
    const gl::GLImplementationParts& implementation) {
  if (!LoadDefaultEGLGLES2Bindings(implementation)) {
    LOG(ERROR) << "QnxGLOzoneEGL: LoadDefaultEGLGLES2Bindings failed";
    return false;
  }
  initialized_ = true;
  DLOG(INFO) << "QnxGLOzoneEGL: GL bindings loaded";
  return true;
}

scoped_refptr<gl::GLSurface> QnxGLOzoneEGL::CreateViewGLSurface(
    gl::GLDisplay* display,
    gfx::AcceleratedWidget widget) {
  // GPU process does not have access to browser-owned screen_window_t.
  // Use an offscreen pbuffer surface for GPU-side rendering instead.
  // The actual DMAbuf export uses eglCreateDRMImageMESA which does not
  // require a window surface.
  NOTREACHED()
      << "QnxGLOzoneEGL: GPU process cannot create a window surface; "
         "use CreateOffscreenGLSurface";
  return nullptr;
}

scoped_refptr<gl::GLSurface> QnxGLOzoneEGL::CreateOffscreenGLSurface(
    gl::GLDisplay* display,
    const gfx::Size& size) {
  if (!initialized_) {
    LOG(ERROR) << "QnxGLOzoneEGL::CreateOffscreenGLSurface: GL bindings "
                  "not initialized";
    return nullptr;
  }
  return gl::InitializeGLSurface(
      base::MakeRefCounted<gl::PbufferGLSurfaceEGL>(
          display->GetAs<gl::GLDisplayEGL>(), size));
}

}  // namespace ui
