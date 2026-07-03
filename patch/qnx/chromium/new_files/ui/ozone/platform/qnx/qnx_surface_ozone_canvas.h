// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_SURFACE_OZONE_CANVAS_H_
#define UI_OZONE_PLATFORM_QNX_QNX_SURFACE_OZONE_CANVAS_H_

#include <memory>
#include <screen/screen.h>

#include "base/component_export.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

class SkCanvas;

namespace ui {

// QnxSurfaceOzoneCanvas is a software SurfaceOzoneCanvas that backs the
// ozone_demo renderer when GL/EGL acceleration is unavailable or not wired.
//
// It wraps a Skia SkSurface backed by an RGBA pixel buffer. On each
// PresentCanvas call, it looks up the screen_window_t for the widget via
// QnxWindowManager and posts the pixel data to QNX Screen via
// screen_post_window().
//
// This is Phase 5 bounded demo smoke: it exercises the full browser-side
// Screen window + software pixel path without requiring the GPU render
// producer or Mojo DMAbuf transport.
class COMPONENT_EXPORT(OZONE_BASE) QnxSurfaceOzoneCanvas
    : public SurfaceOzoneCanvas {
 public:
  // Construct a canvas for |widget|. The widget must have an associated
  // QnxWindow registered with QnxWindowManager.
  explicit QnxSurfaceOzoneCanvas(gfx::AcceleratedWidget widget);
  ~QnxSurfaceOzoneCanvas() override;

  QnxSurfaceOzoneCanvas(const QnxSurfaceOzoneCanvas&) = delete;
  QnxSurfaceOzoneCanvas& operator=(const QnxSurfaceOzoneCanvas&) = delete;

  // SurfaceOzoneCanvas:
  SkCanvas* GetCanvas() override;
  void ResizeCanvas(const gfx::Size& viewport_size, float scale) override;
  void PresentCanvas(const gfx::Rect& damage) override;
  std::unique_ptr<gfx::VSyncProvider> CreateVSyncProvider() override;

 private:
  // Post the current pixel buffer to QNX Screen for |screen_win_|.
  // Returns true on success, false if screen_win_ is null or posting fails.
  bool PostToScreen();

  gfx::AcceleratedWidget widget_;
  screen_window_t screen_win_ = nullptr;
  sk_sp<SkSurface> surface_;
  gfx::Size last_size_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_SURFACE_OZONE_CANVAS_H_
