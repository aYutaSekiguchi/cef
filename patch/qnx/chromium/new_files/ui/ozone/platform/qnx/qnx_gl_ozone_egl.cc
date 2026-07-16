// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 5: GPU-side EGL/GLES2 bindings for QNX.
// See docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md.

#include "ui/ozone/platform/qnx/qnx_gl_ozone_egl.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

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
  QNX_GPU_TRACE_LOG(INFO) << "QnxGLOzoneEGL: GL bindings loaded";
  return true;
}

scoped_refptr<gl::GLSurface> QnxGLOzoneEGL::CreateViewGLSurface(
    gl::GLDisplay* display,
    gfx::AcceleratedWidget widget) {
  // The GPU process does not have access to browser-owned screen_window_t.
  // QNX OOP GPU architecture requires an offscreen surface for GPU-side
  // rendering. Some Chromium code paths (e.g. image_transport_surface_linux.cc
  // for non-Presenter paths, or sandboxed renderer GL init) still request
  // a view surface from the GPU process; on QNX this is not possible because
  // the screen_window_t pointer is never valid in the GPU process.
  //
  // Fall back to a small offscreen pbuffer so the call does not crash.
  // The real GPU work should be using CreateOffscreenGLSurface explicitly.
  DLOG(WARNING) << "QnxGLOzoneEGL: CreateViewGLSurface called in OOP GPU; "
                  "falling back to offscreen pbuffer";
  if (!initialized_) {
    LOG(ERROR) << "QnxGLOzoneEGL::CreateViewGLSurface: GL bindings "
                  "not initialized";
    return nullptr;
  }
  return CreateOffscreenGLSurface(display, gfx::Size(1, 1));
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
