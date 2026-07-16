// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/qnx/qnx_surface_ozone_canvas.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

#include <algorithm>
#include <cstring>
#include <screen/screen.h>

#include "base/check.h"
#include "base/logging.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkSurfaceProps.h"
#include "ui/ozone/platform/qnx/qnx_screen_context.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"

namespace ui {

namespace {

// QNX Screen pixel format that matches Skia's kN32_SkColorType (BGRA/RGBA).
// QNX Screen uses SCREEN_FORMAT_RGBA8888 where R is the MSB of each 32-bit
// pixel, which on a little-endian x86_64 QEMU guest matches SkColor's
// BGRA ordering in memory (0xAABBGGRR).
constexpr int kScreenFormat = SCREEN_FORMAT_RGBA8888;

}  // namespace

QnxSurfaceOzoneCanvas::QnxSurfaceOzoneCanvas(gfx::AcceleratedWidget widget)
    : widget_(widget) {
  // Look up the screen_window_t from QnxWindowManager. This works because
  // QnxSurfaceFactoryOzone is created in InitializeUI (same process as
  // QnxWindowManager) and CreateCanvasForWidget is called from the same
  // process during ozone_demo startup.
  QnxWindowManager* wm = QnxWindowManager::GetInstance();
  if (wm) {
    const QnxWidgetRecord* record = wm->GetWidgetRecord(widget_);
    if (record) {
      screen_win_ = static_cast<screen_window_t>(record->screen_win);
    }
  }
  QNX_GPU_TRACE_LOG(INFO) << "QnxSurfaceOzoneCanvas: widget=" << widget_
             << " screen_win=" << static_cast<void*>(screen_win_);
}

QnxSurfaceOzoneCanvas::~QnxSurfaceOzoneCanvas() = default;

SkCanvas* QnxSurfaceOzoneCanvas::GetCanvas() {
  return surface_ ? surface_->getCanvas() : nullptr;
}

void QnxSurfaceOzoneCanvas::ResizeCanvas(const gfx::Size& viewport_size,
                                         float scale) {
  if (viewport_size.IsEmpty()) {
    surface_.reset();
    last_size_ = gfx::Size();
    return;
  }

  SkImageInfo info = SkImageInfo::Make(
      viewport_size.width(), viewport_size.height(), kN32_SkColorType,
      kPremul_SkAlphaType, SkColorSpace::MakeSRGB());

  // SkSurfaces::Raster allocates CPU pixel memory internally.
  // This is safe for the demo software canvas path.
  surface_ = SkSurfaces::Raster(info);
  last_size_ = viewport_size;

  QNX_GPU_TRACE_LOG(INFO) << "QnxSurfaceOzoneCanvas::ResizeCanvas: " << viewport_size.ToString();
}

void QnxSurfaceOzoneCanvas::PresentCanvas(const gfx::Rect& damage) {
  if (!screen_win_ || !surface_ || last_size_.IsEmpty())
    return;

  // Get the window's current render buffer (the front-buffer for display).
  screen_buffer_t buf = nullptr;
  int rc = screen_get_window_property_pv(screen_win_,
                                          SCREEN_PROPERTY_RENDER_BUFFERS,
                                          reinterpret_cast<void**>(&buf));
  if (rc != 0 || !buf) {
    PLOG(ERROR) << "QnxSurfaceOzoneCanvas: failed to get render buffer";
    return;
  }

  // Get the buffer's pixel memory properties.
  void* buf_ptr = nullptr;
  int stride = 0;
  rc = screen_get_buffer_property_pv(buf, SCREEN_PROPERTY_POINTER, &buf_ptr);
  if (rc != 0) {
    PLOG(ERROR) << "QnxSurfaceOzoneCanvas: failed to get buffer pointer";
    return;
  }
  rc = screen_get_buffer_property_iv(buf, SCREEN_PROPERTY_STRIDE, &stride);
  if (rc != 0) {
    PLOG(ERROR) << "QnxSurfaceOzoneCanvas: failed to get buffer stride";
    return;
  }

  if (!buf_ptr || stride <= 0) {
    LOG(ERROR) << "QnxSurfaceOzoneCanvas: invalid buffer (ptr=" << buf_ptr
               << " stride=" << stride << ")";
    return;
  }

  // Read back the rendered pixels from the Skia surface.
  SkPixmap pixmap;
  if (!surface_->peekPixels(&pixmap)) {
    LOG(ERROR) << "QnxSurfaceOzoneCanvas: peekPixels failed";
    return;
  }

  // Blit from Skia surface to the Screen buffer row by row.
  // The demo window uses SCREEN_USAGE_OPENGL_ES2 which creates RGBA buffers.
  // In QEMU Mesa/virgl the buffer is CPU-accessible via shared memory.
  // If the buffer is GPU-only and this copy fails silently, the demo still
  // runs and exercises the software canvas path.
  const uint8_t* src = static_cast<const uint8_t*>(pixmap.addr());
  uint8_t* dst = static_cast<uint8_t*>(buf_ptr);
  int src_stride = pixmap.rowBytes();
  int height = std::min(last_size_.height(),
                        static_cast<int>(pixmap.height()));
  int width_bytes = std::min(last_size_.width() * 4,
                             static_cast<int>(pixmap.width() * 4));

  for (int y = 0; y < height; y++) {
    memcpy(dst + y * stride, src + y * src_stride, width_bytes);
  }

  // Notify Screen that the buffer content has been updated.
  rc = screen_post_window(screen_win_, buf, 0, nullptr, 0);
  if (rc != 0) {
    PLOG(ERROR) << "QnxSurfaceOzoneCanvas: screen_post_window failed";
  }
}

std::unique_ptr<gfx::VSyncProvider>
QnxSurfaceOzoneCanvas::CreateVSyncProvider() {
  // QNX Screen vsync is not wired in this bounded demo smoke path.
  return nullptr;
}

}  // namespace ui
