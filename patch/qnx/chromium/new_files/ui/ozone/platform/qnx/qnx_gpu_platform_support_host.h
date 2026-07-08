// Copyright 2026 The Chromium Authors
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_GPU_PLATFORM_SUPPORT_HOST_H_
#define UI_OZONE_PLATFORM_QNX_QNX_GPU_PLATFORM_SUPPORT_HOST_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/ozone/platform/qnx/mojom/qnx_gpu.mojom.h"
#include "ui/ozone/public/gpu_platform_support_host.h"

namespace ui {

class QnxGpuHost;
class QnxWindowManager;

// ====================================================================
// QnxGpuPlatformSupportHost — Browser-side GPU launch bridge
// ====================================================================
// Implements ui::GpuPlatformSupportHost (Ozone launch bridge API) in the
// browser process.  This is the QNX-local equivalent of
// WaylandBufferManagerConnector.
//
// Responsibilities:
// 1. Receive OnGpuServiceLaunched() from GpuHostImpl::InitOzone() when the
//    GPU process is spawned.
// 2. Create/bind the browser-owned QnxGpuHost receiver and obtain a
//    mojo::PendingRemote<QnxGpuHost>.
// 3. Use the supplied binder to bind GPU-side QnxGpuService and call
//    Initialize(host_remote) to hand the browser host remote to the GPU.
// 4. Receive OnChannelDestroyed() when the GPU process exits/restarts and
//    reset the GPU-side service remote.
//
// This preserves the out-of-process GPU architecture because the connection
// is established through Chromium's normal GPU-process launch path.
// No custom Unix socket is added; DMAbuf fds travel as Mojo handle<platform>.
//
// Phase 5 scope (this file):
// - Browser-owned QnxGpuHost binding and QnxGpuService.Initialize() handshake.
// - Reset service remote and mark widgets GPU-detached on channel destroyed.
// - No EGL import, no Screen display, no real SubmitFrame handling.
class QnxGpuPlatformSupportHost : public GpuPlatformSupportHost {
 public:
  // |window_manager| must outlive this object.
  explicit QnxGpuPlatformSupportHost(QnxWindowManager* window_manager);
  ~QnxGpuPlatformSupportHost() override;

  QnxGpuPlatformSupportHost(const QnxGpuPlatformSupportHost&) = delete;
  QnxGpuPlatformSupportHost& operator=(const QnxGpuPlatformSupportHost&) =
      delete;

  // GpuPlatformSupportHost:

  // Called from browser UI thread when the GPU service is launched.
  // GpuHostImpl::InitOzone() calls this after spawning the GPU process.
  void OnGpuServiceLaunched(
      int host_id,
      GpuHostBindInterfaceCallback binder,
      GpuHostTerminateCallback terminate_callback) override;

  // Called from browser UI thread when the GPU channel is destroyed.
  // This happens when the GPU process exits or is restarted.
  void OnChannelDestroyed(int host_id) override;

  // Exposes the browser-owned QnxGpuHost remote for callers that need the
  // PendingRemote (e.g., for rebinding on GPU restart).
  mojo::PendingRemote<ui::ozone::qnx::mojom::QnxGpuHost>
      GetGpuHostPendingRemote();

  // Called by OzonePlatformQnxImpl when GPU is detaching; marks all widgets
  // as GPU-detached and increments generation. Called from browser UI thread.
  void MarkAllWidgetsGpuDetached();

  // After QnxGpuService.Initialize() is called, iterate existing browser
  // windows and send AttachWidget to the GPU for each one.
  void AttachExistingWidgets(int host_id);

  // Phase 6: Helper that runs on the QnxGpuService::Initialize ack callback.
  // 1. Binds the browser-side QnxGpuControl remote (which was created
  //    unbinded in OnGpuServiceLaunched and bound on the GPU side via the
  //    binder at that time).  Deferred to here so AddWindow -> AttachNewWidget
  //    calls fired between OnGpuServiceLaunched and the Initialize ack are
  //    a no-op (gpu_control_remote_ is null), eliminating the cross-interface
  //    Mojo race in step 2: we know the GPU has already bound gpu_host_remote_
  //    and is ready to receive AttachWidget.
  // 2. Calls AttachExistingWidgets to inform the GPU about all windows.
  void BindGpuControlAndAttachExistingWidgets(int host_id);

  // Called by QnxWindowManager::AddWindow when a new QnxWindow is created
  // AFTER the GPU service has connected. AttachExistingWidgets runs once at
  // OnGpuServiceLaunched and only sees widgets that existed at that time, so
  // any widget created later (e.g., content_shell's about:blank window)
  // must trigger its own AttachWidget. No-op when the GPU isn't connected
  // yet (early-boot widget creation) or when the widget record is missing.
  void AttachNewWidget(gfx::AcceleratedWidget widget);

 private:
  // Resets the GPU-side QnxGpuService remote. Called on channel destroyed.
  // Also marks all widgets as GPU-detached and increments generation.
  void ResetGpuServiceAndDetach();

  // Non-owning pointer to the widget record table.
  // Set in the constructor; QnxWindowManager outlives this connector.
  raw_ptr<QnxWindowManager> window_manager_;

  // Browser-owned QnxGpuHost implementation. Created in
  // OnGpuServiceLaunched and owned for the lifetime of this connector.
  // A single Receiver (not ReceiverSet) is used because GPU process
  // restarts are handled by destroying and recreating this connector.
  std::unique_ptr<QnxGpuHost> qnx_gpu_host_;

  // Remote to the GPU-side QnxGpuService. Set in OnGpuServiceLaunched when
  // the GPU-side service receiver is bound via the binder. Reset on
  // OnChannelDestroyed.
  mojo::Remote<ui::ozone::qnx::mojom::QnxGpuService> gpu_service_remote_;

  // Remote to the GPU-side QnxGpuControl. The browser holds the client end
  // and sends AttachWidget/ResizeWidget/DetachWidget to the GPU.
  //
  // Phase 6: gpu_control_remote_ is NOT bound by OnGpuServiceLaunched (the
  // pipe handle is sent to the GPU via the binder, but the browser-side
  // Remote is left unbound).  It is bound later by
  // BindGpuControlAndAttachExistingWidgets, which is called only after the
  // GPU process has dispatched QnxGpuService::Initialize(host_remote) and
  // acked.  This guarantees the GPU has bound gpu_host_remote_ before any
  // AttachWidget call lands on the gpu_control pipe, removing the
  // cross-interface Mojo race.
  mojo::Remote<ui::ozone::qnx::mojom::QnxGpuControl> gpu_control_remote_;

  // Pending remote for QnxGpuControl obtained in OnGpuServiceLaunched via
  // mojo::MakeRequest(QnxGpuControl) and stored until the Initialize ack
  // arrives.  The companion PendingReceiver is sent to the GPU via the
  // binder immediately, so the GPU-side QnxGpuService::BindQnxGpuControl
  // is bound before any AttachWidget can be sent.  When the GPU replies
  // to Initialize (the ack callback), BindGpuControlAndAttachExistingWidgets
  // binds this pending remote into gpu_control_remote_ — only then does
  // AttachNewWidget find a bound remote and start dispatching AttachWidget.
  // Reset by BindGpuControlAndAttachExistingWidgets (when host_id matches)
  // or by ResetGpuServiceAndDetach (on disconnect).
  mojo::PendingRemote<ui::ozone::qnx::mojom::QnxGpuControl>
      gpu_control_pending_remote_;

  // The host_id of the current GPU channel. -1 if no GPU is connected.
  int host_id_ = -1;

  // Browser UI thread enforcement.
  THREAD_CHECKER(ui_thread_checker_);

  base::WeakPtrFactory<QnxGpuPlatformSupportHost> weak_factory_{this};
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_GPU_PLATFORM_SUPPORT_HOST_H_
