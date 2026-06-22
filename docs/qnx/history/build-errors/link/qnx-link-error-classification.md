# QNX cefsimple link error classification

## Purpose

After enabling the desktop UI Views source block, the `libcef.so` link of
cefsimple fails with 117 distinct undefined references. Each reference is
classified into one of three buckets before any patch is written:

1. **Root fix** — widen an `IS_LINUX || IS_CHROMEOS` guard to `IS_QNX`
   (or reuse the Linux implementation under a `!is_qnx` exclusion), and
   add the QNX platform file/stub only when the Linux code path cannot
   be reused.
2. **Feature disable** — gate the source/feature and its callers behind
   a build flag (e.g. `ENABLE_PRINTING`, `BUILDFLAG(ENABLE_DICE_SUPPORT)`)
   that is already off on QNX, or remove the chrome feature inclusion
   entirely.
3. **Stub** — add a minimal no-op QNX implementation under
   `cef/patch/qnx/chromium/new_files/...` so the symbol resolves and
   future QNX-native work can fill in the implementation.

## Bucket assignments

| Category | Symbols | Class | Reason |
|---|---|---|---|
| **printing** (8) | `printing::PrintSettings::*`, `printing::NupParameters`, `printing::ConvertUnit*`, `printing::PageSetup::GetSymmetricalPrintableArea` | **2. Disable** | `args.gn` sets `enable_printing=false` and `enable_print_preview=false`. The printing module is being referenced from chrome/browser/ui sources that were widened to QNX. The callers and the printing targets need to be excluded for QNX. |
| **crashpad** (7) | `crashpad::CrashpadClient::*`, `crashpad::CrashReportDatabase::*`, `crashpad::Paths::Executable`, `crashpad::HTTPTransport::Create` | **3. Stub** | QNX uses `use_crash_key_stubs=true` and a `crashpad_qnx` no-op integration path. The remaining `crashpad::*` platform clients that Chromium itself pulls in (via `chrome/browser` targets) need a QNX no-op `crashpad_client_qnx.cc` and `crash_report_database_qnx.cc`. |
| **gfx/ui/views/blink** (18) | `views::DesktopWindowTreeHostPlatform::*`, `views::MenuConfig::InitPlatform`, `views::UnhandledKeyboardEventHandler::HandleNativeKeyboardEvent`, `ui::CreateSelectFileDialog`, `ui::ResourceBundle::GetNativeImageNamed/LoadCommonResources`, `ui::CalculateIdleTime/CheckIdleStateIsLocked/AddScreenLockCallback`, `gfx::Animation::UpdatePrefersReducedMotion/ScrollAnimationsEnabledBySystem/ShouldRenderRichAnimationImpl`, `blink::FontCache::SystemFontFamily/PlatformFallbackFontForCharacter` | **1. Root fix** | Most live in `ui/views/widget/desktop_window_tree_host_platform.cc` (linux), `ui/views/controls/menu/menu_config_linux.cc`, `ui/aura/platform_window/linux/*`, `ui/shell_dialogs/select_file_dialog_linux.cc`, `ui/gfx/animation/animation_linux.cc`, `blink/renderer/platform/fonts/linux/*`. Widen the IS_LINUX guards to IS_QNX so QNX reuses the Linux implementations. `ui::ResourceBundle` linux impl is in `ui/base/resource/resource_bundle_linux.cc`. |
| **chrome/browser platform** (28) | `MemoryDetails::*`, `FirstRunService::*`, `ProfileManagementDisclaimerService*`, `BrowserNativeWidgetFactory` vtable, `whats_new::CreateWhatsNewRegistry`, `DownloadStatusUpdater::UpdateAppIconDownloadProgress`, `FirefoxImporter::FirefoxImporter`, `AutoStart::GetAutostartDirectory`, `IconLoader::*`, `StatusTray::Create`, `NotificationPlatformBridge::*`, `device::TimeZoneMonitor::Create`, `storage_monitor::StorageMonitor::CreateInternal`, `first_run::internal::InitialPrefsPath`, `upgrade_util::RelaunchChromeBrowserImpl`, `GetWindowIcon`, `VersionUpdater::Create` | **1. Root fix / 3. Stub mix** | `MemoryDetails` lives in `chrome/browser/memory_details.cc` (linux guard). `FirstRunService`/`ProfileManagementDisclaimerService`/`ProfileManagementDisclaimerServiceFactory` are chrome/browser/enterprise sources only built for `is_linux || is_chromeos`. `whats_new` is a chrome feature only built for desktop; `DownloadStatusUpdater`/`FirefoxImporter`/`AutoStart`/`IconLoader`/`StatusTray`/`NotificationPlatformBridge` are platform-specific. Root fix: include the linux sources on QNX. Stub: add QNX impls for `IconLoader`/`StatusTray`/`NotificationPlatformBridge`/`TimeZoneMonitor`/`StorageMonitor` if linux code cannot be reused. |
| **enterprise** (10) | `enterprise_signals::*`, `enterprise_connectors::*`, `enterprise_idle::*`, `policy::BatterySaverPolicyHandler` | **2. Disable** | `enterprise_*` and `enterprise_idle` are chrome enterprise features not built on QNX. Their cpp sources need to be excluded from QNX desktop targets (or the chrome feature flags that gate them need to be disabled). |
| **gpu/media** (7) | `gpu::ImageTransportSurface::CreateNativeGLSurface/CreatePresenter`, `gpu::CollectContextGraphicsInfo/CollectBasicGraphicsInfo`, `media::VideoCaptureGpuChannelHost::*` | **1. Root fix** | Linux impls exist in `gpu/config/gpu_info_collector_linux.cc` and `media/video/capture/video_capture_gpu_*_linux.cc`. QNX uses Ozone but doesn't have a real GPU; widen IS_LINUX guards to reuse the linux path (the code is mostly shell-out to `glxinfo`/`vulkaninfo`/etc, which can no-op). |
| **platform_util/base** (7) | `platform_util::OpenExternal/ShowItemInFolder/PlatformOpenVerifiedItem`, `base::GetFileDriveInfo/GetProcessExecutablePath`, `base::ThreadDelegatePosix::Create`, `base::CheckPThreadStackMinIsSafe` | **1. Root fix** | `platform_util` linux impl lives in `chrome/browser/platform_util_linux.cc`. `base::GetFileDriveInfo`/`GetProcessExecutablePath` are in `base/files/file_util_linux.cc`/`base/process/process_handle_linux.cc`. Widen IS_LINUX guards. `ThreadDelegatePosix::Create`/`CheckPThreadStackMinIsSafe` are in `base/threading/platform_thread_posix.cc` and `base/threading/thread_local_posix.cc`. |
| **on_device** (5) | `on_device_model::PreSandboxInit/Shutdown/OnDeviceModelService::Create`, `on_device_internals::OnDeviceInternalsUI::kWebUIControllerType/BindInterface` | **2. Disable** | The QNX build sets `use_dawn=false` and `enable_on_device_model_qnx` defaults to false, but `chrome/browser/ui/webui/on_device_internals` is still in the desktop UI source block. Exclude the on_device_internals webui from the QNX desktop source list. |
| **net** (3) | `net::PlatformMimeUtil::GetPlatform*` | **1. Root fix** | `net::PlatformMimeUtil` has `platform_mime_util_linux.cc` already; the chrome `Build` list only added it for the apple/win/android/fuchsia/ios branches. Add QNX to the same guard so the linux impl is compiled. |
| **policy** (2) | `policy::path_parser::ExpandPathVariables`, `policy::BatterySaverPolicyHandler` | **1. Root fix / 2. Disable mix** | `path_parser` is in `components/policy/core/common/path_parser_linux.cc` (linux only). Widen IS_LINUX guard. `BatterySaverPolicyHandler` is enterprise; disable via the enterprise feature flag. |
| **payments** (1) | `payments::GetBrowserBoundKeyStoreInstance` | **3. Stub** | The browser-bound key store is a payment feature. Provide a no-op QNX stub returning an empty result. |
| **passwords_ui** (2) | `PasswordCrossDomainConfirmationPopupControllerImpl` | **2. Disable** | This UI is excluded on QNX already by `password_credential_ui_controller_reauth_qnx`; the cross-domain confirmation popup is part of the same chrome feature set. Add the QNX guard to the cpp file. |
| **extensions** (2) | `extensions::CpuInfoProvider::QueryCpuTimePerProcessor`, `extensions::RemovableStorageProvider::PopulateDeviceList` | **3. Stub** | Both have platform-specific sources (linux/chromeos/mac/win). Provide QNX stubs. |
| **syncer** (1) | `syncer::GetPersonalizableDeviceNameInternal` | **1. Root fix** | Lives in `components/sync_device_info/local_device_info_util_linux.cc` (or similar). Widen the linux guard. |
| **spelling** (1) | `SpellingOptionsSubMenuObserver` | **2. Disable** | This is a RenderViewContextMenu observer for spell-check options. Gate behind `BUILDFLAG(ENABLE_SPELLCHECK)` which is off on QNX. |
| **render_view** (1) | `RenderViewContextMenu::IsLinkToIsolatedWebApp` | **1. Root fix** | Add QNX guard in the same way as the other `Is*ToIsolatedWebApp` checks. |
| **other** (14) | `vtable for BrowserNativeWidgetFactory`, `vtable for on_device_internals::OnDeviceInternalsUIConfig`, `ChromeAppWindowClient::CreateNativeAppWindowImpl`, `ChromeViewsDelegate::CreateNativeWidget`, `SkFontConfigInterface::RefGlobal`, `ExtensionInstallUIDesktop::ExtensionInstallUIDesktop`, `GetWindowIcon`, `VersionUpdater::Create`, `upgrade_util::RelaunchChromeBrowserImpl`, `first_run::internal::InitialPrefsPath`, `device::TimeZoneMonitor::Create`, `storage_monitor::StorageMonitor::CreateInternal` | **1. Root fix / 3. Stub mix** | Mix of browser widget factory vtables (need QNX widget factory), ChromeAppWindowClient (extensions app window, exclude on QNX), ChromeViewsDelegate::CreateNativeWidget (use linux impl), SkFontConfigInterface::RefGlobal (stub returns nullptr), ExtensionInstallUIDesktop (extensions install UI, stub), GetWindowIcon/VersionUpdater (chrome/browser ui source, use linux), first_run::InitialPrefsPath (stub), TimeZoneMonitor/StorageMonitor (linux impl reused). |

## Execution order

The order below keeps the build graph coherent (dependencies, chrome
desktop UI exclusions, and the on_device webui removal must come first):

1. **Disable (2)**: printing, enterprise, on_device, passwords_ui,
   spelling — guard callers and remove webuis from QNX desktop sources.
2. **Root fix (1)**: gfx/ui/views/blink, platform_util/base, net,
   policy, syncer, render_view, gpu/media, chrome/browser platform
   (MemoryDetails/FirstRunService/ProfileManagementDisclaimerService/whats_new/DownloadStatusUpdater/FirefoxImporter/AutoStart), other (ChromeViewsDelegate, GetWindowIcon, VersionUpdater, upgrade_util, first_run::InitialPrefsPath, TimeZoneMonitor, StorageMonitor).
3. **Stub (3)**: crashpad, payments, extensions (CpuInfoProvider,
   RemovableStorageProvider), BrowserNativeWidgetFactory,
   on_device_internals (after webui removal),
   SkFontConfigInterface::RefGlobal, ExtensionInstallUIDesktop,
   IconLoader, StatusTray, NotificationPlatformBridge.

## Search hints

```bash
rg -n "libcef link undefined references" docs/qnx
rg -n "link fix strategy" docs/qnx
```
