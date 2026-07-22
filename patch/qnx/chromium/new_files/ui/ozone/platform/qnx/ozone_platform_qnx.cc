// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 4: Browser/UI-side QNX Ozone Screen skeleton.
// - Owns visible screen_window_t via QnxWindow.
// - Owns screen_context_t via QnxScreenContext.
// - Allocates stable gfx::AcceleratedWidget IDs via QnxWindowManager.
// - Provides minimal QnxScreen for display enumeration.
// - EGL display/composition surface setup deferred to Phase 5.
// - GPU producer / Mojo frame transport handled in Phase 5 with:
//   Browser-side QnxGpuPlatformSupportHost (GpuPlatformSupportHost bridge).
//   Browser-owned QnxGpuHost receiver (receives SubmitFrame from GPU).
//   GPU-side QnxGpuService receiver (receives browser host remote on launch).

#include "ui/ozone/platform/qnx/ozone_platform_qnx.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "mojo/public/cpp/bindings/binder_map.h"
#include "ui/base/cursor/cursor_factory.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/ozone/layout/stub/stub_keyboard_layout_engine.h"
#include "ui/ozone/platform/qnx/qnx_gl_ozone_egl.h"
#include "ui/ozone/platform/qnx/qnx_gpu_host.h"
#include "ui/ozone/platform/qnx/qnx_gpu_platform_support_host.h"
#include "ui/ozone/platform/qnx/qnx_gpu_service.h"
#include "ui/ozone/platform/qnx/qnx_render_producer.h"
#include "ui/ozone/platform/qnx/qnx_surface_factory.h"
#include "ui/base/ime/input_method_minimal.h"
#include "ui/display/types/native_display_delegate.h"
#include "ui/ozone/common/bitmap_cursor_factory.h"
#include "ui/ozone/common/stub_overlay_manager.h"
#include "ui/ozone/platform/qnx/qnx_screen.h"
#include "ui/ozone/platform/qnx/qnx_screen_context.h"
#include "ui/ozone/platform/qnx/qnx_platform_event_source.h"
#include "ui/ozone/platform/qnx/qnx_window.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/input_controller.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/platform_screen.h"
#include "ui/ozone/public/stub_input_controller.h"
#include "ui/ozone/public/system_input_injector.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace ui {
// Shortcut for the QNX mojom namespace generated from ui.ozone.qnx.mojom.
namespace qnx = ui::ozone::qnx::mojom;

namespace {

// Phase 4/5 QNX Ozone platform implementation.
// Replaces the Phase 3 stub that returned false from InitializeUI().
class OzonePlatformQnxImpl : public OzonePlatform {
 public:
  OzonePlatformQnxImpl() = default;

  OzonePlatformQnxImpl(const OzonePlatformQnxImpl&) = delete;
  OzonePlatformQnxImpl& operator=(const OzonePlatformQnxImpl&) = delete;

  ~OzonePlatformQnxImpl() override = default;

  // OzonePlatform:

  ui::SurfaceFactoryOzone* GetSurfaceFactoryOzone() override {
    // SurfaceFactoryOzone is returned from both browser and GPU processes.
    //
    // Browser process (InitializeUI path): browser_surface_factory_ is
    // created in InitializeUI(). It provides CreateCanvasForWidget() for the
    // ozone_demo software canvas path. GPU producer resources are not needed.
    //
    // GPU process (InitializeGPU path): gpu_surface_factory_ is created and
    // provides GetGLOzone() and CreateNativePixmap() for the GPU render
    // producer.
    //
    // Chromium's GPU initialization calls InitializeGPU() on the same
    // OzonePlatform instance. When both factories exist, prefer the GPU one
    // (the ozone_demo software path will fall back to browser_surface_factory_
    // if called from the browser process before GPU init).
    if (gpu_surface_factory_) {
      return gpu_surface_factory_.get();
    }
    return browser_surface_factory_.get();
  }

  ui::OverlayManagerOzone* GetOverlayManager() override {
    return overlay_manager_.get();
  }

  ui::CursorFactory* GetCursorFactory() override {
    return cursor_factory_.get();
  }

  ui::InputController* GetInputController() override {
    return input_controller_.get();
  }

  ui::GpuPlatformSupportHost* GetGpuPlatformSupportHost() override {
    return gpu_platform_support_host_.get();
  }

  std::unique_ptr<ui::SystemInputInjector> CreateSystemInputInjector() override {
    return nullptr;
  }

  std::unique_ptr<PlatformWindow> CreatePlatformWindow(
      PlatformWindowDelegate* delegate,
      PlatformWindowInitProperties properties) override {
    if (!screen_context_->is_valid()) {
      LOG(ERROR) << "OzonePlatformQnx: cannot create window: "
                    "Screen context is invalid";
      return nullptr;
    }
    return std::make_unique<QnxWindow>(delegate, window_manager_.get(),
                                       screen_context_.get(),
                                       properties.bounds);
  }

  std::unique_ptr<display::NativeDisplayDelegate> CreateNativeDisplayDelegate()
      override {
    return nullptr;
  }

  std::unique_ptr<PlatformScreen> CreateScreen() override {
    return std::make_unique<QnxScreen>(screen_context_.get(),
                                       window_manager_.get());
  }

  void InitScreen(PlatformScreen* screen) override {}

  std::unique_ptr<InputMethod> CreateInputMethod(
      ImeKeyEventDispatcher* ime_key_event_dispatcher,
      gfx::AcceleratedWidget widget) override {
    return std::make_unique<InputMethodMinimal>(ime_key_event_dispatcher);
  }

  bool IsWindowCompositingSupported() const override {
    // EGL display composition is wired and proven: SubmitFrame reaches
    // eglSwapBuffers, display_ok=true confirmed. Return true so the
    // Views compositor initializes and browser init can complete.
    return true;
  }

  bool InitializeUI(const InitParams& params) override {
    // Phase 4: Initialize QNX Screen context and window manager.
    // This is the critical change from Phase 3: InitializeUI() now succeeds.
    screen_context_ = std::make_unique<QnxScreenContext>();
    if (!screen_context_->is_valid()) {
      LOG(ERROR) << "OzonePlatformQnx::InitializeUI: "
                    "QnxScreenContext initialization failed";
      return false;
    }

    window_manager_ = std::make_unique<QnxWindowManager>();

    // Create the event source. It takes ownership of screen_context_t and
    // QnxWindowManager references for the duration of its lifetime.
    // The PlatformEventSource base class registers this as the thread-local
    // singleton on construction.
    event_source_ = std::make_unique<QnxPlatformEventSource>(
        screen_context_->context(), window_manager_.get());
    if (!QnxPlatformEventSource::GetInstance()) {
      LOG(ERROR) << "OzonePlatformQnx::InitializeUI: "
                    "QnxPlatformEventSource construction failed";
      return false;
    }

    // Start the event polling loop. The message pump task runner is available
    // at this point (InitializeUI runs after the task executor is created).
    event_source_->Start();

    overlay_manager_ = std::make_unique<StubOverlayManager>();
    input_controller_ = std::make_unique<StubInputController>();
    cursor_factory_ = std::make_unique<BitmapCursorFactory>();
    keyboard_layout_engine_ = std::make_unique<StubKeyboardLayoutEngine>();
    KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(
        keyboard_layout_engine_.get());

    // Phase 5: Create the QNX GpuPlatformSupportHost bridge.
    // This connector owns the browser-owned QnxGpuHost and handles
    // OnGpuServiceLaunched / OnChannelDestroyed to bind/unbind the GPU-side
    // QnxGpuService.  GetGpuPlatformSupportHost() returns this.
    gpu_platform_support_host_ =
        std::make_unique<QnxGpuPlatformSupportHost>(window_manager_.get());

    // Phase 5: Create browser-local surface factory for ozone_demo software
    // canvas. This factory provides CreateCanvasForWidget() which wraps
    // screen_post_window pixel posting. GPU producer resources (GLOzone) are
    // not needed here.
    browser_surface_factory_ = std::make_unique<QnxSurfaceFactoryOzone>();

    QNX_GPU_TRACE_LOG(INFO) << "OzonePlatformQnx::InitializeUI: success";
    return true;
  }

  // OzonePlatform:

  // AddInterfaces is called from VizMainImpl in both the browser and GPU
  // processes after InitializeUI and InitializeGPU have both run.
  //
  // In the browser process: QnxGpuPlatformSupportHost::OnGpuServiceLaunched
  // (called via GetGpuPlatformSupportHost) handles the binding of the
  // browser-owned QnxGpuHost and GPU-side QnxGpuService.  No binding is done
  // directly in AddInterfaces for the browser side.
  //
  // In the GPU process (has_initialized_ui_==false): bind the GPU-side
  // QnxGpuService and QnxGpuControl receivers to the already-created
  // gpu_service_ instance (created in InitializeGPU with the surface factory).
  // This follows the DRM/AddInterfaces pattern.
  void AddInterfaces(mojo::BinderMap* binders) override {
    if (!has_initialized_ui()) {
      // GPU process: gpu_service_ was created in InitializeGPU() with
      // access to gpu_surface_factory_.  Bind both receiver pipes here.
      //
      // gpu_service_ is a unique_ptr owned by this class.
      // It is valid because InitializeGPU runs before AddInterfaces in the
      // GPU process.
      if (!gpu_service_) {
        LOG(ERROR) << "OzonePlatformQnx::AddInterfaces: gpu_service_ is null; "
                      "InitializeGPU may not have run";
        return;
      }

      // Register QnxGpuService binder: browser obtains the GPU service remote
      // through OnGpuServiceLaunched and calls Initialize(host_remote).
      scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner =
          base::SingleThreadTaskRunner::GetCurrentDefault();

      binders->Add<qnx::QnxGpuService>(
          base::BindRepeating(
              [](QnxGpuService* service,
                 mojo::PendingReceiver<qnx::QnxGpuService> receiver) {
                service->Bind(std::move(receiver));
              },
              gpu_service_.get()),
          gpu_task_runner);

      // Register QnxGpuControl binder: browser sends the QnxGpuControl
      // pipe via binder.Run() in OnGpuServiceLaunched.  The pipe arrives
      // at the GPU synchronously and is bound to the existing gpu_service_
      // instance.  Since both binder callbacks run on the GPU main thread,
      // gpu_service_ is valid when this binder fires.
      binders->Add<qnx::QnxGpuControl>(
          base::BindRepeating(
              [](QnxGpuService* service,
                 mojo::PendingReceiver<qnx::QnxGpuControl> receiver) {
                service->BindQnxGpuControl(std::move(receiver));
              },
              gpu_service_.get()),
          gpu_task_runner);

      QNX_GPU_TRACE_LOG(INFO) << "OzonePlatformQnx::AddInterfaces: QnxGpuService and "
                    "QnxGpuControl binders registered on GPU process; "
                    "QnxGpuService owns QnxRenderProducerManager with "
                    "access to gpu_surface_factory_="
                 << static_cast<void*>(gpu_surface_factory_.get());
    }
  }

  void InitializeGPU(const InitParams& params) override {
    // Phase 5: Initialize GPU-side EGL display, surface factory, and GPU
    // service.  This runs in the GPU process when Chromium spawns the GPU
    // process.  The same OzonePlatform instance is used across processes.

    // ---- GPU-side QnxSurfaceFactoryOzone ----
    if (gpu_surface_factory_) {
      QNX_GPU_TRACE_LOG(INFO) << "OzonePlatformQnx::InitializeGPU: already initialized";
      return;
    }

    gpu_surface_factory_ = QnxSurfaceFactoryOzone::CreateForGpu();
    if (!gpu_surface_factory_) {
      LOG(ERROR) << "OzonePlatformQnx::InitializeGPU: "
                    "QnxSurfaceFactoryOzone construction failed";
      return;
    }

    // ---- GPU-side QnxGpuService ----
    // Create the GPU service with access to the surface factory.
    // The service owns QnxRenderProducerManager and manages GPU-side render
    // producer lifecycle (AttachWidget/ResizeWidget/DetachWidget).
    // It is owned by this class as a unique_ptr; destruction is safe because
    // OzonePlatform outlives the GPU process.
    gpu_service_ = std::make_unique<QnxGpuService>(
        gpu_surface_factory_.get());

    QNX_GPU_TRACE_LOG(INFO) << "OzonePlatformQnx::InitializeGPU: QnxSurfaceFactoryOzone "
                  "and QnxGpuService (with QnxRenderProducerManager) created; "
                  "gpu_surface_factory_="
               << static_cast<void*>(gpu_surface_factory_.get())
               << " gpu_service_=" << static_cast<void*>(gpu_service_.get());
  }

 private:
  std::unique_ptr<QnxScreenContext> screen_context_;
  std::unique_ptr<QnxWindowManager> window_manager_;
  std::unique_ptr<QnxPlatformEventSource> event_source_;
  std::unique_ptr<OverlayManagerOzone> overlay_manager_;
  std::unique_ptr<CursorFactory> cursor_factory_;
  std::unique_ptr<InputController> input_controller_;
  std::unique_ptr<StubKeyboardLayoutEngine> keyboard_layout_engine_;

  // Phase 5: Browser-side surface factory for software demo canvas.
  // Created in InitializeUI(). Provides CreateCanvasForWidget() for the
  // ozone_demo software renderer. Null in the GPU process.
  std::unique_ptr<QnxSurfaceFactoryOzone> browser_surface_factory_;

  // Phase 5: Browser-side GpuPlatformSupportHost bridge.
  // Created in InitializeUI().  QnxGpuPlatformSupportHost owns the
  // browser-owned QnxGpuHost and handles the GPU launch handshake.
  std::unique_ptr<QnxGpuPlatformSupportHost> gpu_platform_support_host_;

  // Phase 5: GPU-side surface factory.  Created in InitializeGPU() (GPU
  // process).  The browser process (InitializeUI) does not create this.
  std::unique_ptr<QnxSurfaceFactoryOzone> gpu_surface_factory_;

  // Phase 5: GPU-side QnxGpuService.  Created in InitializeGPU() (GPU
  // process) with access to gpu_surface_factory_.  Owned as a unique_ptr
  // by this class; destruction cleans up all QnxRenderProducer instances.
  std::unique_ptr<QnxGpuService> gpu_service_;
};

}  // namespace

OzonePlatform* CreateOzonePlatformQnx() {
  return new OzonePlatformQnxImpl();
}

}  // namespace ui
