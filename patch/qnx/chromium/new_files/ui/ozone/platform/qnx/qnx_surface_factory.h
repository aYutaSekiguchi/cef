// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_SURFACE_FACTORY_H_
#define UI_OZONE_PLATFORM_QNX_QNX_SURFACE_FACTORY_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/ozone/public/gl_ozone.h"
#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {

// QnxSurfaceFactoryOzone is the SurfaceFactoryOzone subclass used in the GPU
// process. It owns the GPU-side EGL display and provides the GLOzone
// implementation for GLES2 rendering and DMAbuf export.
//
// Phase 5 scope:
// - Provides GLOzone (QnxGLOzoneEGL) for GPU-side EGL/GLES2.
// - EGL display initialization via InitializeGPU().
// - DMAbuf export scaffold via QnxRenderProducer (runtime calls deferred).
//
// Browser-side Screen window ownership and display composition are Phase 4
// territory and are handled by QnxWindow/QnxScreen in the browser process.
class QnxGLOzoneEGL;

class QnxSurfaceFactoryOzone : public SurfaceFactoryOzone {
 public:
  // Browser-process constructor: no GLOzone, used for ozone_demo software canvas.
  QnxSurfaceFactoryOzone();
  // GPU-process constructor: creates with GLOzone.
  static std::unique_ptr<QnxSurfaceFactoryOzone> CreateForGpu();
  ~QnxSurfaceFactoryOzone() override;

  QnxSurfaceFactoryOzone(const QnxSurfaceFactoryOzone&) = delete;
  QnxSurfaceFactoryOzone& operator=(const QnxSurfaceFactoryOzone&) = delete;

  // SurfaceFactoryOzone:
  std::vector<gl::GLImplementationParts> GetAllowedGLImplementations()
      override;
  GLOzone* GetGLOzone(
      const gl::GLImplementationParts& implementation) override;
  // Creates a software canvas backed by a Skia SkSurface that posts
  // pixel data to the QNX Screen window associated with |widget|.
  // This is the Phase 5 bounded demo smoke path for ozone_demo.
  std::unique_ptr<SurfaceOzoneCanvas> CreateCanvasForWidget(
      gfx::AcceleratedWidget widget) override;

  scoped_refptr<gfx::NativePixmap> CreateNativePixmap(
      gfx::AcceleratedWidget widget,
      gpu::VulkanDeviceQueue* device_queue,
      gfx::Size size,
      viz::SharedImageFormat format,
      gfx::BufferUsage usage,
      std::optional<gfx::Size> framebuffer_size) override;

  // Returns the owned GLOzone instance (non-owning).
  QnxGLOzoneEGL* gl_ozone() const { return gl_ozone_.get(); }

 private:
  // Non-owning GLOzone for the GPU process path.  In the browser process
  // (InitializeUI) this is null and CreateCanvasForWidget provides the
  // demo software path instead.
  std::unique_ptr<QnxGLOzoneEGL> gl_ozone_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_SURFACE_FACTORY_H_
