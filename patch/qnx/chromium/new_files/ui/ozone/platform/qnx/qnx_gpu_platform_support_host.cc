// Copyright 2026 The Chromium Authors
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 5: Browser-side QNX GpuPlatformSupportHost connector.
// Implements ui::GpuPlatformSupportHost (Ozone GPU launch bridge API) to
// bridge the browser-owned QnxGpuHost to the GPU-side QnxGpuService.
// Pattern follows WaylandBufferManagerConnector.

#include "ui/ozone/platform/qnx/qnx_gpu_platform_support_host.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "ui/ozone/platform/qnx/qnx_gpu_host.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"

namespace ui {
namespace qnx = ui::ozone::qnx::mojom;

QnxGpuPlatformSupportHost::QnxGpuPlatformSupportHost(
    QnxWindowManager* window_manager)
    : window_manager_(window_manager) {
  DLOG(INFO) << "QnxGpuPlatformSupportHost: constructed (browser process)";
}

QnxGpuPlatformSupportHost::~QnxGpuPlatformSupportHost() {
  // DLOG to capture destruction even if the window manager is already gone.
  DLOG(INFO) << "QnxGpuPlatformSupportHost: destroyed (browser process)";
  // Explicitly reset the service remote and detach widgets before destruction.
  ResetGpuServiceAndDetach();
}

// ======================================================================
// GpuPlatformSupportHost implementation
// ======================================================================

void QnxGpuPlatformSupportHost::OnGpuServiceLaunched(
    int host_id,
    GpuHostBindInterfaceCallback binder,
    GpuHostTerminateCallback terminate_callback) {
  DCHECK_CALLED_ON_VALID_THREAD(ui_thread_checker_);

  LOG(INFO) << "[QNX-TRACE] OnGpuServiceLaunched: host_id=" << host_id
            << " starting (gpu_control_remote_.is_bound="
            << (gpu_control_remote_.is_bound() ? "true" : "false") << ")";
  DLOG(INFO) << "QnxGpuPlatformSupportHost::OnGpuServiceLaunched: host_id="
             << host_id;

  // Reset any previous GPU connection before establishing a new one.
  ResetGpuServiceAndDetach();

  host_id_ = host_id;

  // ---- Step 1: Create the browser-owned QnxGpuHost ----
  // The browser process owns the QnxGpuHost receiver throughout the GPU
  // process lifetime.  QnxGpuHost::GetPendingRemote() returns a PendingRemote
  // that we pass to the GPU-side QnxGpuService.
  qnx_gpu_host_ = std::make_unique<QnxGpuHost>(window_manager_);

  // Get the PendingRemote<QnxGpuHost> to hand to the GPU process.
  mojo::PendingRemote<qnx::QnxGpuHost> host_remote =
      qnx_gpu_host_->GetPendingRemote();
  if (!host_remote) {
    DLOG(ERROR) << "QnxGpuPlatformSupportHost::OnGpuServiceLaunched: "
                   "QnxGpuHost::GetPendingRemote() returned null; "
                   "cannot connect to GPU";
    std::move(terminate_callback).Run(
        "QnxGpuHost: GetPendingRemote returned null");
    return;
  }

  // ---- Step 2: Bind GPU-side QnxGpuService using the supplied binder ----
  // |binder| is a callback that binds a PendingReceiver to the GPU process.
  // This is the same mechanism WaylandBufferManagerConnector uses.
  //
  // FIX (from Phase 5 attach/generation substep): Call
  // BindNewPipeAndPassReceiver() ONCE to get the receiver pipe.
  // The previous Phase 5 binding code called it twice (once implicitly and
  // once explicitly via PassPipe()), creating two separate pipes.
  // Now we get the receiver once and pass its pipe to the binder.
  // The remote is bound to the same pipe and can be used to call methods.
  mojo::PendingReceiver<qnx::QnxGpuService> service_receiver =
      gpu_service_remote_.BindNewPipeAndPassReceiver();
  binder.Run(qnx::QnxGpuService::Name_, service_receiver.PassPipe());

  if (!gpu_service_remote_) {
    DLOG(ERROR) << "QnxGpuPlatformSupportHost::OnGpuServiceLaunched: "
                   "binder failed to bind QnxGpuService";
    std::move(terminate_callback).Run(
        "QnxGpuService: binder failed");
    return;
  }

  // Set disconnect handler to log GPU-side service pipe closure.
  gpu_service_remote_.set_disconnect_handler(base::BindOnce([]() {
    DLOG(INFO) << "QnxGpuPlatformSupportHost: GPU QnxGpuService pipe "
                  "disconnected";
  }));

  // ---- Step 3: Create both halves of the QnxGpuControl pipe ----
  // We use PendingRemote<>::InitWithNewPipeAndPassReceiver() to create a
  // (PendingRemote, PendingReceiver) pair.  The PendingReceiver is sent to
  // the GPU via the binder (so the GPU process's QnxGpuService::BindQnxGpuControl
  // receives it), and the PendingRemote is stored as gpu_control_pending_remote_
  // until the QnxGpuService::Initialize ack arrives.  Once acked (see
  // BindGpuControlAndAttachExistingWidgets) we Bind the PendingRemote into
  // gpu_control_remote_ — before that, gpu_control_remote_.is_bound()
  // returns false and AttachNewWidget is a no-op.
  //
  // This deferral eliminates the cross-interface Mojo race: gpu_control is
  // not client-bindable until the browser knows the GPU has dispatched
  // QnxGpuService::Initialize and acked.
  {
    mojo::PendingReceiver<qnx::QnxGpuControl> control_receiver_pending =
        gpu_control_pending_remote_.InitWithNewPipeAndPassReceiver();
    binder.Run(qnx::QnxGpuControl::Name_,
               control_receiver_pending.PassPipe());
  }

  // ---- Step 4: Call QnxGpuService::Initialize() with the browser host ----
  // This hands the browser-owned QnxGpuHost remote to the GPU process.
  // After this, the GPU can call SubmitFrame and ReportProducerLost.
  //
  // Phase 6: Initialize is ack-style. We chain the deferred
  // BindGpuControlAndAttachExistingWidgets to the ack so the GPU cannot
  // receive AttachWidget on the gpu_control pipe before Initialize has been
  // dispatched AND the GPU has bound gpu_host_remote_.  This eliminates
  // the cross-interface Mojo ordering race that previously required --v=1
  // to mask.
  gpu_service_remote_->Initialize(
      std::move(host_remote),
      base::BindOnce(
          &QnxGpuPlatformSupportHost::BindGpuControlAndAttachExistingWidgets,
          base::Unretained(this), host_id));

  DLOG(INFO) << "QnxGpuPlatformSupportHost::OnGpuServiceLaunched: "
                "QnxGpuService::Initialize() called (with ack callback); "
                "BindGpuControlAndAttachExistingWidgets will fire after GPU "
                "acks; QnxGpuControl binder has been sent but browser-side "
                "binding is deferred; GPU can call SubmitFrame and receive "
                "QnxGpuControl via AddInterfaces";
}

void QnxGpuPlatformSupportHost::BindGpuControlAndAttachExistingWidgets(
    int host_id) {
  DCHECK_CALLED_ON_VALID_THREAD(ui_thread_checker_);
  LOG(INFO) << "[QNX-TRACE] BindGpuControlAndAttachExistingWidgets: "
               "Initialize ack received; binding gpu_control_remote_ now";
  DLOG(INFO) << "QnxGpuPlatformSupportHost::BindGpuControlAndAttachExistingWidgets:"
                " host_id=" << host_id;

  if (host_id_ != host_id) {
    DLOG(WARNING) << "BindGpuControlAndAttachExistingWidgets: mismatched "
                     "host_id=" << host_id << " (expected " << host_id_
                  << "); ignoring (GPU may have been replaced)";
    gpu_control_pending_remote_ = mojo::NullRemote();
    return;
  }

  // Now bind the browser-side gpu_control_remote_ from the pending remote
  // that OnGpuServiceLaunched created with
  // PendingRemote::InitWithNewPipeAndPassReceiver().
  // After this call, AddWindow -> AttachNewWidget will find a bound remote
  // and will dispatch AttachWidget through the gpu_control pipe.  Because
  // we are post-Initialize-ack, the GPU has already bound gpu_host_remote_,
  // so the GPU-side AttachWidget guard is satisfied and the test trigger
  // (SubmitTestFrameForWidget) will fire as expected.
  if (gpu_control_pending_remote_.is_valid()) {
    gpu_control_remote_.Bind(std::move(gpu_control_pending_remote_));
    DLOG(INFO) << "BindGpuControlAndAttachExistingWidgets: gpu_control_remote_"
                  " bound after Initialize ack";
  } else {
    DLOG(WARNING) << "BindGpuControlAndAttachExistingWidgets: no pending "
                     "QnxGpuControl remote; binding step skipped";
  }

  // Iterate all browser-owned widgets created before the GPU connected
  // and inform the GPU of their current state.
  AttachExistingWidgets(host_id);
}

void QnxGpuPlatformSupportHost::OnChannelDestroyed(int host_id) {
  DCHECK_CALLED_ON_VALID_THREAD(ui_thread_checker_);

  LOG(INFO) << "[QNX-TRACE] OnChannelDestroyed: host_id=" << host_id
            << " (GPU process exited or channel broken; will reset and let "
               "Chromium respawn a fresh GPU)";
  DLOG(INFO) << "QnxGpuPlatformSupportHost::OnChannelDestroyed: host_id="
             << host_id;

  if (host_id_ != host_id) {
    DLOG(WARNING) << "QnxGpuPlatformSupportHost::OnChannelDestroyed: "
                     "mismatched host_id="
                  << host_id << " (expected " << host_id_
                  << "); ignoring";
    return;
  }

  // Reset GPU service remote and mark all widgets GPU-detached.
  ResetGpuServiceAndDetach();
  host_id_ = -1;
}

// ======================================================================
// Public helpers
// ======================================================================

mojo::PendingRemote<qnx::QnxGpuHost>
QnxGpuPlatformSupportHost::GetGpuHostPendingRemote() {
  if (qnx_gpu_host_) {
    return qnx_gpu_host_->GetPendingRemote();
  }
  return mojo::PendingRemote<qnx::QnxGpuHost>();
}

void QnxGpuPlatformSupportHost::MarkAllWidgetsGpuDetached() {
  if (!window_manager_) {
    DLOG(WARNING) << "QnxGpuPlatformSupportHost::MarkAllWidgetsGpuDetached: "
                     "no window_manager_; nothing to detach";
    return;
  }
  // Increment generation for all widgets so any stale frames from the old
  // GPU process are discarded.  Uses DetachAllWidgets() which atomically
  // marks all widgets GPU-detached and increments their generation.
  window_manager_->DetachAllWidgets();
  DLOG(INFO) << "QnxGpuPlatformSupportHost::MarkAllWidgetsGpuDetached: done; "
                "all widgets detached, generations incremented";
}

void QnxGpuPlatformSupportHost::AttachExistingWidgets(int host_id) {
  if (host_id_ != host_id) {
    DLOG(WARNING) << "AttachExistingWidgets: mismatched host_id=" << host_id
                  << " (expected " << host_id_ << "); ignoring";
    return;
  }

  if (!window_manager_) {
    DLOG(WARNING) << "AttachExistingWidgets: no window_manager_";
    return;
  }

  if (!gpu_control_remote_ || !gpu_control_remote_.is_bound()) {
    LOG(INFO) << "[QNX-TRACE] AttachExistingWidgets: gpu_control_remote_ not "
                 "bound yet (deferred Initialize binding)";
    DLOG(WARNING) << "AttachExistingWidgets: no GPU control remote; "
                     "cannot attach widgets";
    return;
  }

  std::vector<QnxWidgetRecord> records =
      window_manager_->GetWidgetRecordsForTestingOrGpuAttach();

  if (records.empty()) {
    LOG(INFO) << "[QNX-TRACE] AttachExistingWidgets: no existing widgets";
    DLOG(INFO) << "AttachExistingWidgets: no existing widgets";
    return;
  }

  LOG(INFO) << "[QNX-TRACE] AttachExistingWidgets: " << records.size()
            << " existing widget(s); sending AttachWidget for each";
  DLOG(INFO) << "AttachExistingWidgets: " << records.size()
             << " existing widget(s)";

  for (const auto& record : records) {
    if (record.gpu_attached) {
      DLOG(WARNING) << "AttachExistingWidgets: widget=" << record.id
                     << " is already GPU-attached; skipping";
      continue;
    }

    // Atomically increment generation and mark GPU-attached in the record.
    window_manager_->IncrementGeneration(record.id);
    window_manager_->SetGpuAttached(record.id, true, 0);

    // Read back the new generation from the updated record.
    const QnxWidgetRecord* updated =
        window_manager_->GetWidgetRecord(record.id);
    uint32_t new_gen = updated ? updated->generation : 1u;

    // Send AttachWidget to the GPU process so it can create GPU-side
    // render resources for this widget.  The GPU receives this on its
    // QnxGpuControl receiver (implemented by QnxGpuService).
    gpu_control_remote_->AttachWidget(record.id, new_gen, record.size);

    DLOG(INFO) << "AttachExistingWidgets: widget=" << record.id
               << " generation=0 -> " << new_gen
               << " GPU-attached; AttachWidget(size=" << record.size.width()
               << "x" << record.size.height() << ") sent to GPU";
  }
}

void QnxGpuPlatformSupportHost::AttachNewWidget(
    gfx::AcceleratedWidget widget) {
  DCHECK_CALLED_ON_VALID_THREAD(ui_thread_checker_);

  // No GPU connected yet (gpu_control_remote_ not bound yet) -> AttachExistingWidgets
  // will pick this widget up when the GPU launches and BindGpuControlAndAttachExistingWidgets
  // fires. This is the common early-boot case.
  if (!gpu_control_remote_ || !gpu_control_remote_.is_bound()) {
    LOG(INFO) << "[QNX-TRACE] AttachNewWidget: widget=" << widget
              << " gpu_control_remote_ not bound yet (Initialize ack in flight); "
                 "AttachExistingWidgets will handle after ack";
    DLOG(INFO) << "AttachNewWidget: widget=" << widget
               << " gpu_control_remote_ not bound yet (Initialize ack in flight); "
                  "AttachExistingWidgets will handle after ack";
    return;
  }

  if (!window_manager_) {
    DLOG(WARNING) << "AttachNewWidget: no window_manager_";
    return;
  }

  const QnxWidgetRecord* record = window_manager_->GetWidgetRecord(widget);
  if (!record) {
    DLOG(WARNING) << "AttachNewWidget: no record for widget=" << widget;
    return;
  }

  if (record->gpu_attached) {
    DLOG(WARNING) << "AttachNewWidget: widget=" << widget
                   << " is already GPU-attached; skipping";
    return;
  }

  // Same bookkeeping as AttachExistingWidgets: increment generation,
  // mark GPU-attached, then read back the new generation.
  window_manager_->IncrementGeneration(widget);
  window_manager_->SetGpuAttached(widget, true, 0);
  const QnxWidgetRecord* updated = window_manager_->GetWidgetRecord(widget);
  uint32_t new_gen = updated ? updated->generation : 1u;

  gpu_control_remote_->AttachWidget(widget, new_gen, record->size);

  DLOG(INFO) << "AttachNewWidget: widget=" << widget
             << " generation=0 -> " << new_gen
             << " GPU-attached; AttachWidget(size=" << record->size.width()
             << "x" << record->size.height() << ") sent to GPU";
}

// ======================================================================
// Private helpers
// ======================================================================

void QnxGpuPlatformSupportHost::ResetGpuServiceAndDetach() {
  // Reset the GPU-side QnxGpuService remote first (GPU process exit path).
  if (gpu_service_remote_) {
    DLOG(INFO) << "QnxGpuPlatformSupportHost::ResetGpuServiceAndDetach: "
                  "resetting GPU service remote";
    gpu_service_remote_.reset();
  }

  // Reset the GPU-side QnxGpuControl remote.
  if (gpu_control_remote_) {
    DLOG(INFO) << "QnxGpuPlatformSupportHost::ResetGpuServiceAndDetach: "
                  "resetting GPU control remote";
    gpu_control_remote_.reset();
  }

  // Mark all widgets as GPU-detached and increment generation so any
  // stale frames from the old GPU process are discarded.
  MarkAllWidgetsGpuDetached();

  // Destroy the browser-owned QnxGpuHost.  A fresh one will be created
  // for the next GPU connection (OnGpuServiceLaunched).
  qnx_gpu_host_.reset();

  DLOG(INFO) << "QnxGpuPlatformSupportHost::ResetGpuServiceAndDetach: done; "
                "widgets detached, host destroyed";
}

}  // namespace ui
