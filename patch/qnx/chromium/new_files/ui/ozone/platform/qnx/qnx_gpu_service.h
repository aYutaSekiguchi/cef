// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_GPU_SERVICE_H_
#define UI_OZONE_PLATFORM_QNX_QNX_GPU_SERVICE_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/ozone/platform/qnx/mojom/qnx_gpu.mojom.h"

namespace ui {

// Shortcut for the QNX mojom namespace generated from ui.ozone.qnx.mojom.
namespace qnx = ui::ozone::qnx::mojom;

class QnxRenderProducerManager;
class QnxSurfaceFactoryOzone;
class QnxWindowManager;

// Native QnxDmaBufFrame struct (defined in qnx_render_producer.h).
// Distinct from ui::ozone::qnx::mojom::QnxDmaBufFrame.
struct QnxDmaBufFrame;

// ====================================================================
// QnxGpuService — GPU-side receiver for browser host remote
// ====================================================================
// This class implements the QnxGpuService Mojo interface from the GPU
// process.  It is bound via OzonePlatformQnx::AddInterfaces() and
// receives the browser-owned QnxGpuHost remote through Initialize().
//
// Phase 5 scope (this file):
// - Stores the mojo::Remote<QnxGpuHost> received from the browser.
// - Tiny accessor and logging only.
// - No real SubmitFrame producer calls (deferred to later Phase 5 substep).
//
// GPU-side render producer logic and DMAbuf export are deferred to
// subsequent Phase 5 substeps.
class QnxGpuService : public qnx::QnxGpuService,
                      public qnx::QnxGpuControl {
 public:
  // Constructs the GPU-side QnxGpuService.
  //
  // |surface_factory| must be the GPU-side QnxSurfaceFactoryOzone instance,
  // which owns the EGL display and GL context needed by QnxRenderProducer.
  // |surface_factory| must outlive this object.
  //
  // The GPU process does not own Screen windows or widget records.
  // The mojo::Remote<QnxGpuHost> is populated during Initialize().
  explicit QnxGpuService(QnxSurfaceFactoryOzone* surface_factory);
  ~QnxGpuService() override;

  QnxGpuService(const QnxGpuService&) = delete;
  QnxGpuService& operator=(const QnxGpuService&) = delete;

  // Binds a pending receiver to this implementation.
  // Called by OzonePlatformQnxImpl::AddInterfaces when the GPU service
  // is launched. After binding, incoming Mojo calls (Initialize) are
  // dispatched to the override below.
  void Bind(mojo::PendingReceiver<qnx::QnxGpuService> pending_receiver);

  // Returns the stored mojo::Remote<QnxGpuHost>. Valid only after
  // Initialize() has been called with a valid pending_remote.
  mojo::Remote<qnx::QnxGpuHost>& gpu_host_remote() {
    return gpu_host_remote_;
  }

  // Binds a pending receiver for QnxGpuControl.  Called by
  // OzonePlatformQnxImpl::AddInterfaces when the browser sends the
  // QnxGpuControl pipe via binder.Run().  This is separate from Bind()
  // because QnxGpuControl arrives via a different binder callback.
  void BindQnxGpuControl(
      mojo::PendingReceiver<qnx::QnxGpuControl> pending_receiver);

  // qnx::QnxGpuService:
  void Initialize(mojo::PendingRemote<qnx::QnxGpuHost> host_remote) override;

  // qnx::QnxGpuControl: GPU-side handler for widget lifecycle messages
  // from the browser.  The browser calls these methods on its
  // mojo::Remote<QnxGpuControl> to inform the GPU process of attach/resize/detach.
  // Phase 5: creates/resizes/destroys QnxRenderProducer instances and
  // exercises a metadata+export-only SubmitFrame call.  The conversion function
  // NativeFrameToMojomFrame lives in the .cc file (not the .h) to avoid
  // ambiguity between ui::QnxDmaBufFrame and ui::ozone::qnx::mojom::QnxDmaBufFrame.
  void AttachWidget(gfx::AcceleratedWidget widget,
                    uint32_t generation,
                    const gfx::Size& size) override;
  void ResizeWidget(gfx::AcceleratedWidget widget,
                    uint32_t generation,
                    const gfx::Size& size) override;
  void DetachWidget(gfx::AcceleratedWidget widget,
                    uint32_t generation) override;

  // Exercises the GPU-side render producer pipeline by calling
  // QnxRenderProducer::CreateExportFrame() for the given widget+generation
  // and submitting it to the browser host if the remote is valid and the
  // frame has planes.
  //
  // This is a compile-safe export-only exercise path.  The browser host
  // returns accepted=false with "import/display deferred" for valid frames.
  // No browser EGL import/display is implemented.
  void SubmitTestFrameForWidget(gfx::AcceleratedWidget widget,
                                uint32_t generation);

 private:
  // Converts a native QnxDmaBufFrame (with base::ScopedFD fds) to a
  // mojom QnxDmaBufFramePtr (with mojo::PlatformHandle fds) for Mojo
  // submission.  This is the bridge between the native producer struct
  // (ui::QnxDmaBufFrame) and the Mojo IPC wire format
  // (ui::ozone::qnx::mojom::QnxDmaBufFramePtr).
  // Defined in the .cc file where both headers are available.
  // Takes a non-const reference so the plane ScopedFDs can be std::move'd
  // into the mojo PlatformHandle (avoids a double close / EBADF crash).
  qnx::QnxDmaBufFramePtr NativeFrameToMojomFrame(
      ::ui::QnxDmaBufFrame& frame);

  // Mojo receiver owning the message pipe for this interface.
  // Bound in Bind() when AddInterfaces registers the receiver.
  mojo::Receiver<qnx::QnxGpuService> receiver_{this};

  // Remote to the browser-owned QnxGpuHost. Set during Initialize().
  // The GPU process uses this to call SubmitFrame and ReportProducerLost.
  mojo::Remote<qnx::QnxGpuHost> gpu_host_remote_;

  // Pending receiver for QnxGpuControl. Bound in Initialize().
  // The browser sends AttachWidget/ResizeWidget/DetachWidget calls over
  // this interface.  QnxGpuService implements QnxGpuControl.
  mojo::Receiver<qnx::QnxGpuControl> gpu_control_receiver_{
      static_cast<qnx::QnxGpuControl*>(this)};

  // Non-owning pointer to the GPU-side surface factory.  Used to create
  // QnxRenderProducer instances.  Must outlive |producer_manager_|.
  raw_ptr<QnxSurfaceFactoryOzone> surface_factory_;

  // Owns all per-widget QnxRenderProducer instances.  Created in the
  // constructor using |surface_factory_|.  Destroyed in the destructor
  // to clean up all GPU-side producer resources.
  std::unique_ptr<QnxRenderProducerManager> producer_manager_;

  // Feature flag for bounded SubmitFrame trigger.  When true, AttachWidget
  // and ResizeWidget call SubmitTestFrameForWidget() after producer
  // initialization to exercise the GPU->Browser Mojo SubmitFrame path.
  // This is a scaffold-only flag; no runtime validation in this substep.
  bool enable_attach_test_frame_ = true;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_GPU_SERVICE_H_
