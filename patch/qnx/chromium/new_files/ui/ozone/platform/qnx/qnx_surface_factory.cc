// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 5: GPU-side QNX render producer scaffold.
// See docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md.

#include "ui/ozone/platform/qnx/qnx_surface_factory.h"

#include <memory>

#include "base/logging.h"
#include "ui/ozone/platform/qnx/qnx_gl_ozone_egl.h"
#include "ui/ozone/platform/qnx/qnx_surface_ozone_canvas.h"

namespace ui {

// static
std::unique_ptr<QnxSurfaceFactoryOzone> QnxSurfaceFactoryOzone::CreateForGpu() {
  return std::make_unique<QnxSurfaceFactoryOzone>();
}

QnxSurfaceFactoryOzone::QnxSurfaceFactoryOzone() {
  DLOG(INFO) << "QnxSurfaceFactoryOzone: constructed";
}

std::unique_ptr<SurfaceOzoneCanvas> QnxSurfaceFactoryOzone::CreateCanvasForWidget(
    gfx::AcceleratedWidget widget) {
  // Browser-process software canvas: creates a QnxSurfaceOzoneCanvas that
  // wraps a Skia SkSurface and posts pixel data to QNX Screen on PresentCanvas.
  // The GLOzone/gl_ozone_ path (GPU process) is separate and does not call this.
  return std::make_unique<QnxSurfaceOzoneCanvas>(widget);
}

QnxSurfaceFactoryOzone::~QnxSurfaceFactoryOzone() = default;

std::vector<gl::GLImplementationParts>
QnxSurfaceFactoryOzone::GetAllowedGLImplementations() {
  return std::vector<gl::GLImplementationParts>{
      gl::GLImplementationParts(gl::kGLImplementationEGLANGLE),
  };
}

GLOzone* QnxSurfaceFactoryOzone::GetGLOzone(
    const gl::GLImplementationParts& implementation) {
  // Lazily create gl_ozone_ on first use (GPU process path).
  // In the browser process (browser_surface_factory_) this is never called
  // because InitializeGPU has not run, so gl_ozone_ stays null.
  if (!gl_ozone_) {
    gl_ozone_ = std::make_unique<QnxGLOzoneEGL>();
    DLOG(INFO) << "QnxSurfaceFactoryOzone::GetGLOzone: lazily created "
                  "gl_ozone_ (GPU process)";
  }
  switch (implementation.gl) {
    case gl::kGLImplementationEGLANGLE:
    case gl::kGLImplementationEGLGLES2:
      return gl_ozone_.get();
    default:
      return nullptr;
  }
}

scoped_refptr<gfx::NativePixmap> QnxSurfaceFactoryOzone::CreateNativePixmap(
    gfx::AcceleratedWidget widget,
    gpu::VulkanDeviceQueue* device_queue,
    gfx::Size size,
    viz::SharedImageFormat format,
    gfx::BufferUsage usage,
    std::optional<gfx::Size> framebuffer_size) {
  // Phase 5: DMAbuf-backed NativePixmap via QnxRenderProducer is deferred.
  // Return a null-handle stub so that GN deps resolve; runtime creation
  // requires the full QnxRenderProducer pipeline.
  DLOG(INFO) << "QnxSurfaceFactoryOzone::CreateNativePixmap: deferred to "
                "QnxRenderProducer (widget="
             << widget << ", size=" << size.ToString()
             << ", usage=" << static_cast<int>(usage) << ")";
  return nullptr;
}

}  // namespace ui
