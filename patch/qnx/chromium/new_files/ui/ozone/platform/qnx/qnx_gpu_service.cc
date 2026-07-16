// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 5: GPU-side QNX Mojo service scaffold.
// Implements QnxGpuService and QnxGpuControl in the GPU process.
// QnxGpuService receives the browser-owned QnxGpuHost remote.
// QnxGpuControl receives AttachWidget/ResizeWidget/DetachWidget from the
// browser and manages GPU-side QnxRenderProducer instances.
// A metadata+export-only SubmitFrame exercise path is provided via
// SubmitTestFrameForWidget().  No browser EGL import/display.

#include "ui/ozone/platform/qnx/qnx_gpu_service.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

#include <memory>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/scoped_generic.h"
#include "mojo/public/cpp/platform/platform_handle.h"
#include "ui/ozone/platform/qnx/qnx_render_producer.h"

namespace ui {
namespace qnx = ui::ozone::qnx::mojom;

namespace {

// Diagnostic command-line switch for QNX Ozone GPU trace output.
// When present, emits grep-stable LOG(INFO) lines prefixed with "QNX_OZONE_GPU_TRACE"
// at key GPU-service and GPU-host Mojo IPC boundaries to confirm that the
// out-of-process SubmitFrame path is reached at runtime without requiring
// a visual smoke test.
// Usage: --ozone-qnx-gpu-trace
constexpr char kOzoneQnxGpuTraceSwitch[] = "ozone-qnx-gpu-trace";

bool IsQnxGpuTraceEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      kOzoneQnxGpuTraceSwitch);
}

}  // namespace

QnxGpuService::QnxGpuService(QnxSurfaceFactoryOzone* surface_factory)
    : surface_factory_(surface_factory),
      producer_manager_(std::make_unique<QnxRenderProducerManager>(
          surface_factory)) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService: constructed (GPU process) "
                "surface_factory="
             << static_cast<void*>(surface_factory_);
}

QnxGpuService::~QnxGpuService() {
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService: destroyed (GPU process)";
  // Destroy all GPU-side producer resources before the GPU process exits.
  // This removes all QnxRenderProducer instances managed by producer_manager_.
  if (producer_manager_) {
    producer_manager_->RemoveAllProducers();
  }
  // Reset the remote to ensure the message pipe is closed cleanly.
  gpu_host_remote_.reset();
}

void QnxGpuService::Bind(
    mojo::PendingReceiver<qnx::QnxGpuService> pending_receiver) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::Bind: binding pending receiver";
  receiver_.Bind(std::move(pending_receiver));
}

void QnxGpuService::BindQnxGpuControl(
    mojo::PendingReceiver<qnx::QnxGpuControl> pending_receiver) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::BindQnxGpuControl: binding pending receiver";
  gpu_control_receiver_.Bind(std::move(pending_receiver));
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::BindQnxGpuControl: gpu_control_receiver_ bound; "
                "browser can now call AttachWidget/ResizeWidget/DetachWidget";
}

// ======================================================================
// qnx::mojom::QnxGpuService implementation
// ======================================================================

void QnxGpuService::Initialize(
    mojo::PendingRemote<qnx::QnxGpuHost> host_remote) {
  if (!host_remote) {
    DLOG(ERROR) << "QnxGpuService::Initialize: null host_remote";
    return;
  }

  // Reset any previous connection before binding a new one.
  gpu_host_remote_.reset();

  // Bind the pending remote from the browser process. The GPU process
  // now holds the client end and can call SubmitFrame / ReportProducerLost.
  gpu_host_remote_.Bind(std::move(host_remote));

  // Set a disconnect handler to log when the browser-owned QnxGpuHost
  // pipe is closed (e.g., browser shutdown or GPU process crash).
  gpu_host_remote_.set_disconnect_handler(base::BindOnce([]() {
    QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService: browser QnxGpuHost pipe disconnected";
  }));

  // Note: QnxGpuControl receiver (gpu_control_receiver_) is bound separately
  // in OnGpuServiceLaunched via the binder before Initialize() is called.
  // The browser passes the QnxGpuControl pipe through the binder, which
  // arrives at the GPU-side QnxGpuService::Initialize() via the
  // browser's gpu_control_remote_.Pipe().get() handle.

  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::Initialize: gpu_host_remote bound; "
                "GPU can now call SubmitFrame / ReportProducerLost; "
                "QnxGpuControl receiver bound via binder in AddInterfaces";
  if (IsQnxGpuTraceEnabled()) {
    LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::Initialize: gpu_host_remote"
                 " bound; GPU process is ready to call SubmitFrame";
  }

}

// ======================================================================
// qnx::mojom::QnxGpuControl implementation (GPU-side handlers)
// ======================================================================

void QnxGpuService::AttachWidget(gfx::AcceleratedWidget widget,
                                 uint32_t generation,
                                 const gfx::Size& size) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::AttachWidget: widget=" << widget
             << " generation=" << generation
             << " size=" << size.width() << "x" << size.height();
  if (IsQnxGpuTraceEnabled()) {
    LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=" << widget
              << " generation=" << generation
              << " size=" << size.width() << "x" << size.height();
  }

  if (!producer_manager_) {
    DLOG(ERROR) << "QnxGpuService::AttachWidget: producer_manager_ is null; "
                   "skipping (should not happen)";
    return;
  }

  // Look up or create the producer for this widget+generation.
  // If one already exists (e.g. from a previous AttachWidget for the same
  // generation), GetOrCreateProducer returns the existing one.
  QnxRenderProducer* producer = producer_manager_->GetOrCreateProducer(
      widget, generation, size);
  if (!producer) {
    DLOG(ERROR) << "QnxGpuService::AttachWidget: GetOrCreateProducer "
                   "returned null for widget=" << widget
                << " generation=" << generation;
    return;
  }

  // Initialize the producer if it has not been initialized yet.
  // QnxRenderProducer::Initialize() probes EGL extensions and resolves
  // DMAbuf export function pointers.
  if (!producer->is_valid()) {
    std::string init_result =
        producer->Initialize() ? "success" : "failed: " + producer->init_error();
    QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::AttachWidget: producer->Initialize() "
                  "result for widget="
               << widget << ": " << init_result;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::AttachWidget: widget=" << widget
             << " generation=" << generation
             << " producer=" << static_cast<void*>(producer)
             << " valid=" << producer->is_valid()
             << " init_error="
             << (producer->init_error().empty()
                     ? "(none)"
                     : producer->init_error());

  // ---- Phase 5: bounded SubmitFrame trigger ----
  // After producer is created and (re-)initialized, exercise the GPU->Browser
  // Mojo SubmitFrame path with a test frame.  This validates the fd
  // ownership/move semantics in SubmitTestFrameForWidget at compile time
  // and provides diagnostic output at runtime without requiring full app smoke.
  // Guard: only fire if the browser host remote is bound (AttachExistingWidgets
  // ensures Initialize() was called first) and the producer is valid.
  if (enable_attach_test_frame_ && gpu_host_remote_ && producer->is_valid()) {
    if (IsQnxGpuTraceEnabled()) {
      LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: "
                   "widget=" << widget << " generation=" << generation
                << " TRIGGER SubmitTestFrameForWidget (remote_bound=true "
                   "producer_valid=true)";
    }
    QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::AttachWidget: trigger: calling "
                  "SubmitTestFrameForWidget(widget="
               << widget << ", generation=" << generation << ")";
    SubmitTestFrameForWidget(widget, generation);
  } else if (enable_attach_test_frame_ && !gpu_host_remote_) {
    if (IsQnxGpuTraceEnabled()) {
      LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: "
                   "widget=" << widget << " generation=" << generation
                << " SKIP SubmitTestFrameForWidget (remote_bound=false)";
    }
    DLOG(WARNING) << "QnxGpuService::AttachWidget: enable_attach_test_frame_ "
                     "is true but gpu_host_remote_ is null; skipping "
                     "SubmitTestFrameForWidget (GPU service may not be "
                     "initialized yet)";
  } else if (enable_attach_test_frame_ && !producer->is_valid()) {
    if (IsQnxGpuTraceEnabled()) {
      LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: "
                   "widget=" << widget << " generation=" << generation
                << " SKIP SubmitTestFrameForWidget (producer_valid=false)";
    }
    DLOG(WARNING) << "QnxGpuService::AttachWidget: enable_attach_test_frame_ "
                     "is true but producer is not valid; skipping "
                     "SubmitTestFrameForWidget";
  }
}

void QnxGpuService::ResizeWidget(gfx::AcceleratedWidget widget,
                                 uint32_t generation,
                                 const gfx::Size& size) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::ResizeWidget: widget=" << widget
             << " generation=" << generation
             << " size=" << size.width() << "x" << size.height();

  if (!producer_manager_) {
    DLOG(ERROR) << "QnxGpuService::ResizeWidget: producer_manager_ is null; "
                   "skipping";
    return;
  }

  // Resize is implemented as a remove + recreate for the same generation.
  // This ensures the producer's DRM EGLImage is re-created at the new size.
  // If the producer is absent (e.g. race where resize arrives before attach),
  // GetOrCreateProducer creates a new one.
  producer_manager_->RemoveProducer(widget, generation);
  QnxRenderProducer* producer = producer_manager_->GetOrCreateProducer(
      widget, generation, size);
  if (!producer) {
    DLOG(ERROR) << "QnxGpuService::ResizeWidget: GetOrCreateProducer "
                   "returned null for widget=" << widget
                << " generation=" << generation;
    return;
  }

  if (!producer->is_valid()) {
    std::string init_result =
        producer->Initialize() ? "success" : "failed: " + producer->init_error();
    QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::ResizeWidget: producer->Initialize() "
                  "result for widget="
               << widget << ": " << init_result;
  }

  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::ResizeWidget: widget=" << widget
             << " generation=" << generation
             << " resized to " << size.width() << "x" << size.height();

  // ---- Phase 5: bounded SubmitFrame trigger on ResizeWidget ----
  // After the producer is re-created at the new size, submit a test frame
  // to validate the resized export path.  Same guard as AttachWidget.
  if (enable_attach_test_frame_ && gpu_host_remote_ && producer->is_valid()) {
    if (IsQnxGpuTraceEnabled()) {
      LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::ResizeWidget: "
                   "widget=" << widget << " generation=" << generation
                << " TRIGGER SubmitTestFrameForWidget (remote_bound=true "
                   "producer_valid=true)";
    }
    QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::ResizeWidget: trigger: calling "
                  "SubmitTestFrameForWidget(widget="
               << widget << ", generation=" << generation << ")";
    SubmitTestFrameForWidget(widget, generation);
  } else if (enable_attach_test_frame_ && !gpu_host_remote_) {
    if (IsQnxGpuTraceEnabled()) {
      LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::ResizeWidget: "
                   "widget=" << widget << " generation=" << generation
                << " SKIP SubmitTestFrameForWidget (remote_bound=false)";
    }
    DLOG(WARNING) << "QnxGpuService::ResizeWidget: enable_attach_test_frame_ "
                     "is true but gpu_host_remote_ is null; skipping";
  } else if (enable_attach_test_frame_ && !producer->is_valid()) {
    if (IsQnxGpuTraceEnabled()) {
      LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::ResizeWidget: "
                   "widget=" << widget << " generation=" << generation
                << " SKIP SubmitTestFrameForWidget (producer_valid=false)";
    }
    DLOG(WARNING) << "QnxGpuService::ResizeWidget: enable_attach_test_frame_ "
                     "is true but producer is not valid; skipping";
  }
}

void QnxGpuService::DetachWidget(gfx::AcceleratedWidget widget,
                                  uint32_t generation) {
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::DetachWidget: widget=" << widget
             << " generation=" << generation;

  if (!producer_manager_) {
    DLOG(WARNING) << "QnxGpuService::DetachWidget: producer_manager_ is null; "
                     "nothing to detach";
    return;
  }

  producer_manager_->RemoveProducer(widget, generation);
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::DetachWidget: widget=" << widget
             << " generation=" << generation << " producer removed";
}

// ======================================================================
// SubmitFrame exercise path (export-only)
// ======================================================================

// static
qnx::QnxDmaBufFramePtr QnxGpuService::NativeFrameToMojomFrame(
    ::ui::QnxDmaBufFrame& frame) {
  // Build the mojom frame.  Each plane's base::ScopedFD is transferred
  // (std::move) into a mojo::PlatformHandle for Mojo handle<platform>
  // serialization.  The native frame must be non-const so we can move from
  // its ScopedFD members; calling .get() and constructing a fresh ScopedFD
  // from the raw int would leave both the original ScopedFD and the new
  // one pointing at the same fd, causing a double close() / EBADF on
  // destruction.  See crbug pattern for mojo::PlatformHandle from
  // base::ScopedFD.
  qnx::QnxDmaBufFramePtr mojom_frame = qnx::QnxDmaBufFrame::New();
  mojom_frame->widget = frame.widget;
  mojom_frame->generation = frame.generation;
  mojom_frame->width = frame.width;
  mojom_frame->height = frame.height;
  mojom_frame->fourcc = frame.fourcc;
  mojom_frame->modifier = frame.modifier;

  for (size_t i = 0; i < frame.planes.size(); ++i) {
    QnxDmaBufPlane& native_plane = frame.planes[i];

    // Transfer ownership of the plane fd into the mojo PlatformHandle.
    // After this std::move, native_plane.fd is empty; the matching
    // close() will happen via the mojo serialized handle on the browser
    // side.  This is the supported pattern for mojo::PlatformHandle
    // construction from a base::ScopedFD.
    mojo::PlatformHandle handle(std::move(native_plane.fd));

    qnx::QnxDmaBufPlanePtr mojom_plane(
        std::in_place,
        std::move(handle),
        native_plane.stride,
        native_plane.offset,
        native_plane.size);
    mojom_frame->planes.push_back(std::move(mojom_plane));
  }

  return mojom_frame;
}

void QnxGpuService::SubmitTestFrameForWidget(gfx::AcceleratedWidget widget,
                                             uint32_t generation) {
  if (!producer_manager_) {
    DLOG(ERROR) << "QnxGpuService::SubmitTestFrameForWidget: "
                   "producer_manager_ is null";
    return;
  }

  if (!gpu_host_remote_) {
    DLOG(WARNING) << "QnxGpuService::SubmitTestFrameForWidget: "
                     "gpu_host_remote_ is null; browser host not connected; "
                     "skipping (AttachWidget may not have been called yet)";
    return;
  }

  QnxRenderProducer* producer = producer_manager_->GetProducer(widget, generation);
  if (!producer) {
    DLOG(WARNING) << "QnxGpuService::SubmitTestFrameForWidget: no producer "
                     "for widget="
                  << widget << " generation=" << generation
                  << "; skipping (AttachWidget may not have been called)";
    return;
  }

  if (!producer->is_valid()) {
    DLOG(WARNING) << "QnxGpuService::SubmitTestFrameForWidget: producer "
                     "is not valid for widget="
                  << widget << "; skipping";
    return;
  }

  // Call CreateExportFrame() to exercise the GPU-side DMAbuf export pipeline.
  auto [frame, error] = producer->CreateExportFrame();
  if (!error.empty()) {
    DLOG(ERROR) << "QnxGpuService::SubmitTestFrameForWidget: "
                   "CreateExportFrame failed for widget="
                << widget << ": " << error;
    return;
  }

  if (frame.planes.empty()) {
    DLOG(WARNING) << "QnxGpuService::SubmitTestFrameForWidget: "
                     "CreateExportFrame returned no planes for widget="
                  << widget << "; skipping SubmitFrame";
    return;
  }

  if (IsQnxGpuTraceEnabled()) {
    LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget: "
                 "widget=" << widget << " generation=" << generation
              << " planes=" << frame.planes.size()
              << " size=" << frame.width << "x" << frame.height
              << "; calling gpu_host_remote_->SubmitFrame";
  }
  QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::SubmitTestFrameForWidget: widget=" << widget
             << " generation=" << generation
             << " frame has " << frame.planes.size() << " plane(s)"
             << " size=" << frame.width << "x" << frame.height
             << "; submitting to QnxGpuHost";

  // Convert the native frame to mojom and submit.  NativeFrameToMojomFrame
  // transfers ownership of each plane's ScopedFD into the mojo message,
  // so the original QnxDmaBufFrame's destructor must not try to close them
  // again.  After this call, |frame| must not be reused.
  qnx::QnxDmaBufFramePtr mojom_frame = NativeFrameToMojomFrame(frame);

  gpu_host_remote_->SubmitFrame(
      std::move(mojom_frame),
      base::BindOnce(
          [](gfx::AcceleratedWidget widget, uint32_t generation,
             bool accepted, const std::string& diagnostic) {
            if (IsQnxGpuTraceEnabled()) {
              LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuService::"
                           "SubmitTestFrameForWidget callback: widget="
                        << widget << " generation=" << generation
                        << " accepted=" << accepted
                        << " diagnostic=" << diagnostic;
            }
            QNX_GPU_TRACE_LOG(INFO) << "QnxGpuService::SubmitTestFrameForWidget: "
                          "widget="
                       << widget << " generation=" << generation
                       << " accepted=" << accepted
                       << " diagnostic=" << diagnostic;

          },
          widget, generation));
}

}  // namespace ui
