// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 5: Browser-side QNX Mojo host scaffold with EGL/Screen import.
// Implements QnxGpuHost in the browser process to receive GPU-produced
// DMAbuf frames.  Metadata is validated; QnxFrameImporter handles
// EGL/Screen import and display.

#include "ui/ozone/platform/qnx/qnx_gpu_host.h"

#include <string>

#include "base/logging.h"
#include "ui/ozone/platform/qnx/qnx_frame_importer.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"

namespace ui {
namespace qnx = ui::ozone::qnx::mojom;

QnxGpuHost::QnxGpuHost(QnxWindowManager* window_manager)
    : window_manager_(window_manager) {
  DLOG(INFO) << "QnxGpuHost: constructed";
}

QnxGpuHost::~QnxGpuHost() {
  DLOG(INFO) << "QnxGpuHost: destroyed";
}

mojo::PendingRemote<qnx::QnxGpuHost> QnxGpuHost::GetPendingRemote() {
  // receiver_.BindNewPipeAndPassRemote() creates a new PendingRemote
  // connected to this receiver.  If the receiver is already bound, the
  // existing pipe is replaced (QnxGpuPlatformSupportHost resets the old
  // host before calling this on GPU restart).
  DLOG(INFO) << "QnxGpuHost::GetPendingRemote: creating pending remote";
  return receiver_.BindNewPipeAndPassRemote();
}

void QnxGpuHost::Bind(
    mojo::PendingReceiver<qnx::QnxGpuHost> pending_receiver) {
  DLOG(INFO) << "QnxGpuHost::Bind: binding pending receiver";
  receiver_.Bind(std::move(pending_receiver));
}

// ======================================================================
// Metadata validation
// ======================================================================

std::pair<bool, std::string> QnxGpuHost::ValidateFrameMetadata(
    const qnx::QnxDmaBufFrame& frame) const {
  // ---- Non-null widget ----
  if (frame.widget == gfx::kNullAcceleratedWidget) {
    return {false, "ERROR_NULL_WIDGET: widget is kNullAcceleratedWidget"};
  }

  // ---- Nonzero generation ----
  // Frames with generation==0 should not arrive (generation starts at 1
  // when the GPU first attaches).  Treat as invalid.
  if (frame.generation == 0) {
    return {false,
            "ERROR_ZERO_GENERATION: generation is 0; GPU may not be attached"};
  }

  // ---- Positive dimensions ----
  if (frame.width == 0 || frame.height == 0) {
    return {false,
            "ERROR_ZERO_DIMENSION: width=" + std::to_string(frame.width) +
                " or height=" + std::to_string(frame.height) + " is zero"};
  }

  // ---- Reasonable dimension bounds ----
  // Reject absurdly large frames to avoid allocating huge buffers.
  // 32768 is the maximum texture dimension in many GL implementations.
  constexpr uint32_t kMaxDimension = 32768;
  if (frame.width > kMaxDimension || frame.height > kMaxDimension) {
    return {false,
            "ERROR_DIMENSION_OVERFLOW: width=" + std::to_string(frame.width) +
                " or height=" + std::to_string(frame.height) +
                " exceeds " + std::to_string(kMaxDimension)};
  }

  // ---- Non-empty planes array ----
  if (frame.planes.empty()) {
    return {false, "ERROR_NO_PLANES: frame has no DMAbuf planes"};
  }

  // ---- Per-plane validation ----
  for (size_t i = 0; i < frame.planes.size(); ++i) {
    const auto& plane = frame.planes[i];

    // ---- Platform handle must be present ----
    // A null/empty platform handle means no DMAbuf fd was sent.
    // Note: mojo::PlatformHandle is empty when its fd is invalid.
    if (!plane->fd.is_valid()) {
      return {false,
              "ERROR_PLANE_NO_FD: plane " + std::to_string(i) +
                  " has no valid DMAbuf fd (handle is empty)"};
    }

    // ---- Positive stride ----
    if (plane->stride == 0) {
      return {false,
              "ERROR_PLANE_ZERO_STRIDE: plane " + std::to_string(i) +
                  " has stride=0"};
    }

    // ---- Stride must be >= width*bytes_per_pixel for packed formats ----
    // Approximate check: for ARGB32 (4 bytes/pixel), require stride >= width*4.
    // Skip for multi-plane formats where this heuristic doesn't apply.
    if (frame.planes.size() == 1 && frame.planes[0]->stride < frame.width * 4) {
      DLOG(WARNING) << "QnxGpuHost::ValidateFrameMetadata: plane " << i
                    << " stride=" << plane->stride
                    << " may be less than width*4=" << (frame.width * 4)
                    << " (proceeding anyway for non-ARGB formats)";
    }

    // ---- Non-negative offset ----
    if (plane->offset < 0) {
      return {false,
              "ERROR_PLANE_NEGATIVE_OFFSET: plane " + std::to_string(i) +
                  " has negative offset"};
    }
  }

  // ---- FourCC check (advisory only) ----
  // We don't reject unknown fourcc codes since the GPU may use formats we
  // haven't enumerated.  Log it for diagnostics.
  DLOG(INFO) << "QnxGpuHost::ValidateFrameMetadata: widget=" << frame.widget
             << " generation=" << frame.generation
             << " size=" << frame.width << "x" << frame.height
             << " fourcc=0x" << std::hex << frame.fourcc
             << " modifier=0x" << frame.modifier << std::dec
             << " planes=" << frame.planes.size();

  return {true, std::string()};
}

// ======================================================================
// qnx::mojom::QnxGpuHost implementation
// ======================================================================

void QnxGpuHost::SubmitFrame(qnx::QnxDmaBufFramePtr frame,
                             SubmitFrameCallback callback) {
  if (!frame) {
    DLOG(ERROR) << "QnxGpuHost::SubmitFrame: null frame pointer";
    std::move(callback).Run(false, "ERROR_NULL_FRAME: frame pointer is null");
    return;
  }

  // ---- Step 1: Conservative metadata validation ----
  auto [is_valid, diagnostic] = ValidateFrameMetadata(*frame);
  if (!is_valid) {
    DLOG(ERROR) << "QnxGpuHost::SubmitFrame: validation failed: " << diagnostic;
    std::move(callback).Run(false, diagnostic);
    return;
  }

  // ---- Step 2: Widget existence check ----
  const QnxWidgetRecord* record = nullptr;
  if (window_manager_) {
    record = window_manager_->GetWidgetRecord(frame->widget);
  }
  if (!record) {
    DLOG(ERROR) << "QnxGpuHost::SubmitFrame: no widget record for widget="
               << frame->widget;
    std::move(callback).Run(false,
                            "ERROR_UNKNOWN_WIDGET: no record for widget=" +
                                std::to_string(frame->widget));
    return;
  }

  // ---- Step 3: Generation equality check ----
  // Frames must carry the current widget generation to be accepted.
  // A stale generation means the frame is from a GPU process that has
  // already been replaced (the GPU may have restarted, or this is a late
  // frame arriving after a restart).
  if (frame->generation != record->generation) {
    DLOG(WARNING) << "QnxGpuHost::SubmitFrame: stale generation for widget="
                  << frame->widget << ": frame_gen=" << frame->generation
                  << " record_gen=" << record->generation;
    std::move(callback).Run(
        false, "ERROR_STALE_GENERATION: frame generation=" +
                  std::to_string(frame->generation) +
                  " does not match widget generation=" +
                  std::to_string(record->generation));
    return;
  }

  // ---- Step 4: GPU attachment check ----
  // GPU must be attached before the browser can accept frames.
  // Detachment may occur when the GPU process restarts (generation was
  // already checked above and matched, so this is a legitimate state).
  if (!record->gpu_attached) {
    DLOG(WARNING) << "QnxGpuHost::SubmitFrame: GPU not attached for widget="
                  << frame->widget
                  << " (widget may be detaching or GPU may have restarted)";
    std::move(callback).Run(
        false, "ERROR_GPU_NOT_ATTACHED: widget=" +
                  std::to_string(frame->widget) +
                  " GPU not attached; import/display deferred");
    return;
  }

  // ---- Step 5: Size consistency (advisory) ----
  if (record->size.width() > 0 && record->size.height() > 0) {
    if (static_cast<uint32_t>(record->size.width()) != frame->width ||
        static_cast<uint32_t>(record->size.height()) != frame->height) {
      DLOG(WARNING) << "QnxGpuHost::SubmitFrame: size mismatch for widget="
                    << frame->widget << ": frame=" << frame->width << "x"
                    << frame->height << " widget_record=" << record->size.width()
                    << "x" << record->size.height();
    }
  }

  // ---- Validation passed ----
  // Metadata is valid, widget exists, generation matches, GPU is attached.
  // Attempt EGL/Screen import and display via QnxFrameImporter.

  // ---- Step 6: Lazy-initialize frame importer ----
  if (!frame_importer_) {
    DLOG(INFO) << "QnxGpuHost::SubmitFrame: creating QnxFrameImporter";
    frame_importer_ = std::make_unique<QnxFrameImporter>(window_manager_);
  }

  // ---- Step 7: Import DMAbuf and display on Screen window ----
  // QnxFrameImporter::ImportAndDisplayFrame handles:
  //   - Per-widget EGL display/context/surface creation.
  //   - Mojo PlatformHandle fd extraction.
  //   - eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT) import.
  //   - GLES2 texture binding via glEGLImageTargetTexture2DOES.
  //   - Fullscreen quad draw to Screen window surface.
  //   - eglSwapBuffers to display.
  // Returns {success, diagnostic}: success=true means display was reached;
  // success=false with "scaffold: ..." diagnostic means a known untested
  // path deferred the display.
  auto [display_ok, display_diagnostic] =
      frame_importer_->ImportAndDisplayFrame(frame->widget, *frame);

  if (display_ok) {
    DLOG(INFO) << "QnxGpuHost::SubmitFrame: widget=" << frame->widget
               << " generation=" << frame->generation
               << " import/display scaffold reached eglSwapBuffers; "
                  "display confirmed";
    std::move(callback).Run(true, std::string());
  } else {
    DLOG(WARNING) << "QnxGpuHost::SubmitFrame: widget=" << frame->widget
                  << " import/display deferred: " << display_diagnostic;
    std::move(callback).Run(false, display_diagnostic);
  }
}

void QnxGpuHost::ReportProducerLost(gfx::AcceleratedWidget widget,
                                    uint32_t generation) {
  DLOG(INFO) << "QnxGpuHost::ReportProducerLost: widget=" << widget
             << " generation=" << generation;

  if (!window_manager_) {
    DLOG(ERROR) << "QnxGpuHost::ReportProducerLost: no window_manager_, "
                   "cannot update widget state";
    return;
  }

  // ---- Look up the widget record ----
  const QnxWidgetRecord* record = window_manager_->GetWidgetRecord(widget);
  if (!record) {
    DLOG(WARNING) << "QnxGpuHost::ReportProducerLost: no record for widget="
                  << widget << "; ignoring";
    return;
  }

  // ---- Validate generation ----
  // Only update if the reported generation matches the current known generation.
  // A stale ReportProducerLost (from a GPU process that has already been
  // replaced) should not overwrite the new generation.
  if (generation != record->generation) {
    DLOG(WARNING) << "QnxGpuHost::ReportProducerLost: stale generation for "
                     "widget="
                  << widget << ": reported_gen=" << generation
                  << " current_gen=" << record->generation
                  << "; ignoring (GPU may have already restarted)";
    return;
  }

  // ---- Update widget record ----
  // Mark GPU as detached and increment generation so any pending stale frames
  // are discarded.  The new GPU process will send AttachWidget with the
  // incremented generation.
  // Capture old generation before mutating the record so the log
  // is accurate.
  uint32_t old_generation = record->generation;
  window_manager_->SetGpuAttached(widget, false, 0);
  window_manager_->IncrementGeneration(widget);

  DLOG(INFO) << "QnxGpuHost::ReportProducerLost: widget=" << widget
             << " marked GPU-detached; generation incremented from "
             << old_generation << " to " << record->generation;
}

}  // namespace ui
