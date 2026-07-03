// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_GPU_HOST_H_
#define UI_OZONE_PLATFORM_QNX_QNX_GPU_HOST_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/ozone/platform/qnx/mojom/qnx_gpu.mojom.h"

namespace ui {

// Shortcut for the QNX mojom namespace generated from ui.ozone.qnx.mojom.
namespace qnx = ui::ozone::qnx::mojom;

class QnxFrameImporter;
class QnxWindowManager;

// ====================================================================
// QnxGpuHost — Browser-side receiver for GPU process frame submissions
// ====================================================================
// This class implements the QnxGpuHost Mojo interface from the browser
// process.  The GPU process holds the corresponding client
// (QnxGpuHostRemote) and calls SubmitFrame to deliver rendered DMAbuf
// frames.
//
// Phase 5 scope (this file):
// - Metadata validation.
// - Browser-side EGL/Screen import/display scaffold via QnxFrameImporter.
// - Generation tracking.
// - Diagnostic-only ReportProducerLost (GPU detach handled by
//   QnxGpuPlatformSupportHost).
class QnxGpuHost : public qnx::QnxGpuHost {
 public:
  // |window_manager| must outlive this object (typically owned by the same
  // OzonePlatformQnxImpl that owns this QnxGpuHost).
  explicit QnxGpuHost(QnxWindowManager* window_manager);
  ~QnxGpuHost() override;

  QnxGpuHost(const QnxGpuHost&) = delete;
  QnxGpuHost& operator=(const QnxGpuHost&) = delete;

  // Returns a PendingRemote to this implementation.  Used by the browser
  // process to pass its QnxGpuHost to the GPU-side QnxGpuService during
  // OnGpuServiceLaunched().  If the receiver is already bound, this creates
  // a new pipe (the old one is reset by QnxGpuPlatformSupportHost on GPU
  // restart).  Safe to call even when the receiver is not yet bound.
  mojo::PendingRemote<qnx::QnxGpuHost> GetPendingRemote();

  // Binds a pending receiver to this implementation.
  // Called by OzonePlatformQnxImpl::AddInterfaces when the GPU process
  // connects.  After binding, incoming Mojo calls are dispatched to the
  // SubmitFrame / ReportProducerLost overrides.
  void Bind(mojo::PendingReceiver<qnx::QnxGpuHost> pending_receiver);

  // qnx::QnxGpuHost:
  void SubmitFrame(qnx::QnxDmaBufFramePtr frame,
                   SubmitFrameCallback callback) override;
  void ReportProducerLost(gfx::AcceleratedWidget widget,
                          uint32_t generation) override;

 private:
  // Validates frame metadata conservatively.
  // Returns {is_valid, diagnostic}.  Diagnostic is always populated.
  std::pair<bool, std::string> ValidateFrameMetadata(
      const qnx::QnxDmaBufFrame& frame) const;

  // Non-owning pointer to the widget record table.
  // Set in the constructor; the QnxWindowManager outlives QnxGpuHost.
  raw_ptr<QnxWindowManager> window_manager_;

  // Browser-side EGL/Screen DMAbuf import/display helper.
  // Created lazily on first SubmitFrame call to ensure EGL is available
  // in the browser process.  Owned by this class.
  std::unique_ptr<QnxFrameImporter> frame_importer_;

  // Mojo receiver owning the message pipe for this interface.
  // Bound in Bind() when AddInterfaces registers the receiver.
  mojo::Receiver<qnx::QnxGpuHost> receiver_{this};
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_GPU_HOST_H_
