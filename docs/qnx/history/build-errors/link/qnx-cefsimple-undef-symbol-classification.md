# QNX cefsimple undefined symbol classification

Build: `out/qnx_release cefsimple`
Date: 2026-06-22
Total distinct undefined references: 103

## Category 1: root fix (QNX should provide real implementation)

These symbols are required for QNX functionality. The fix is to add `is_qnx`
to the existing Linux/ChromeOS/Posix source selections so the Linux code is
compiled, or to add a small QNX-specific implementation.

| # | Symbol | Notes |
|---|--------|-------|
| 1 | `views::MenuConfig::InitPlatform()` | `ui/views/BUILD.gn` selects `*_linux.cc` only for `is_linux`. Extend to QNX. |
| 2 | `MemoryDetails::ChromeBrowser()` | `chrome/browser/memory_details.cc` is built but `MemoryDetails` platform pieces are Linux-only. |
| 3 | `MemoryDetails::MemoryDetails()` | same as above |
| 4 | `MemoryDetails::CollectProcessData(...)` | same as above |
| 5 | `views::UnhandledKeyboardEventHandler::HandleNativeKeyboardEvent(...)` | `unhandled_keyboard_event_handler_ozone.cc` is guarded to `is_linux/is_chromeos`. |
| 6 | `net::PlatformMimeUtil::GetPlatformMimeTypeFromExtension(...)` | `net/base/platform_mime_util_linux.cc` not included for QNX. |
| 7 | `net::PlatformMimeUtil::GetPlatformPreferredExtensionForMimeType(...)` | same as above |
| 8 | `net::PlatformMimeUtil::GetPlatformExtensionsForMimeType(...)` | same as above |
| 9 | `DownloadStatusUpdater::UpdateAppIconDownloadProgress(...)` | `download_status_updater_linux.cc` Linux-only. |
| 10 | `gfx::Animation::UpdatePrefersReducedMotion()` | Linux implementation exists, extend guard. |
| 11 | `gfx::Animation::ScrollAnimationsEnabledBySystem()` | same as above |
| 12 | `gfx::Animation::ShouldRenderRichAnimationImpl()` | same as above |
| 13 | `ui::ResourceBundle::GetNativeImageNamed(int)` | resource bundle platform code Linux-only. |
| 14 | `ui::ResourceBundle::LoadCommonResources()` | same as above |
| 15 | `base::GetProcessExecutablePath(int)` | base process path Linux-only. |
| 16 | `IconLoader::ReadIcon()` | `chrome/browser/ui/views/icon_loader_linux.cc` Linux-only. |
| 17 | `IconLoader::GetReadIconTaskRunner()` | same as above |
| 18 | `IconLoader::GroupForFilepath(...)` | same as above |
| 19 | `media::CreateAudioManager(...)` | audio manager Linux implementation. |
| 20 | `gpu::CollectContextGraphicsInfo(...)` | GPU info collection Linux path. |
| 21 | `gpu::CollectBasicGraphicsInfo(...)` | same as above |
| 22 | `gpu::ImageTransportSurface::CreateNativeGLSurface(...)` | GPU surface Linux path. |
| 23 | `gpu::ImageTransportSurface::CreatePresenter(...)` | same as above |
| 24 | `device::TimeZoneMonitor::Create(...)` | device timezone Linux impl. |
| 25 | `memory_instrumentation::OSMetrics::GetProcessMemoryMaps(int)` | OS metrics Linux path. |
| 26 | `memory_instrumentation::OSMetrics::FillOSMemoryDump(...)` | same as above |
| 27 | `storage_monitor::StorageMonitor::CreateInternal()` | storage monitor Linux path. |
| 28 | `ui::CreateSelectFileDialog(...)` | select file dialog Linux impl. |
| 29 | `ui::CalculateIdleTime()` | idle time Linux impl. |
| 30 | `ui::CheckIdleStateIsLocked()` | same as above |
| 31 | `ui::AddScreenLockCallback(...)` | same as above |
| 32 | `base::ThreadDelegatePosix::Create(...)` | base sampling profiler Linux/Posix guard. |
| 33 | `blink::FontCache::SystemFontFamily()` | blink font cache Linux impl. |
| 34 | `blink::FontCache::PlatformFallbackFontForCharacter(...)` | same as above |
| 35 | `blink::LayoutTheme::NativeTheme()` | layout theme Linux impl. |
| 36 | `SkFontConfigInterface::RefGlobal()` | skia font config Linux impl. |
| 37 | `ChromeAppWindowClient::CreateNativeAppWindowImpl(...)` | chrome app window Linux impl. |
| 38 | `StatusTray::Create()` | status tray Linux impl. |
| 39 | `NotificationPlatformBridge::CanHandleType(...)` | notification bridge Linux impl. |
| 40 | `NotificationPlatformBridge::Create()` | same as above |
| 41 | `GetWindowIcon(content::DesktopMediaID)` | desktop media Linux impl. |
| 42 | `HandleOnPerformingDrop(...)` | web contents drop Linux impl. |
| 43 | `ui::OSExchangeDataProviderNonBacked::OSExchangeDataProviderNonBacked()` | drag/drop Linux impl. |
| 44 | `AddContextMenuParamsPropertiesFromPreferences(...)` | context menu Linux impl. |
| 45 | `policy::path_parser::ExpandPathVariables(...)` | policy path expansion Linux impl. |
| 46 | `extensions::CpuInfoProvider::QueryCpuTimePerProcessor(...)` | system_cpu extension Linux impl. |
| 47 | `extensions::RemovableStorageProvider::PopulateDeviceList()` | storage extension Linux impl. |
| 48 | `media::VideoCaptureGpuChannelHost::GetSharedImageInterface()` | video capture Linux path. |
| 49 | `media::VideoCaptureGpuChannelHost::GetInstance()` | same as above |
| 50 | `views::DesktopWindowTreeHostPlatform::GetAllOpenWindows()` | views desktop window platform code. |
| 51 | `views::DesktopWindowTreeHostPlatform::GetContentWindowForWidget(...)` | same as above |
| 52 | `views::DesktopWindowTreeHostPlatform::CleanUpWindowList(...)` | same as above |
| 53 | `ChromeViewsDelegate::CreateNativeWidget(...)` | views delegate Linux impl. |
| 54 | `base::CheckPThreadStackMinIsSafe()` | base pthread stack check Linux impl. |
| 55 | `AutoStart::GetAutostartDirectory(...)` | first-run auto-start Linux impl. |
| 56 | `base::GetFileDriveInfo(...)` | base file drive info Linux impl. |
| 57 | `BrowserNativeWidgetFactory` vtable | native widget factory Linux impl. |

## Category 2: stub for now (feature not needed immediately)

These symbols come from features that QNX may eventually support, but for now
a no-op stub is acceptable. Many are UI flows that CEF can simply not trigger.

| # | Symbol | Notes |
|---|--------|-------|
| 1 | `ProfileMenuCoordinator::ProfileMenuCoordinator(...)` | Avatar/profile bubble UI. Stub or disable caller. |
| 2 | `ProfileMenuCoordinator::Show(...)` | same as above |
| 3 | `ProfileMenuCoordinator::~ProfileMenuCoordinator()` | same as above |
| 4 | `ProfileManagementDisclaimerService::*` (5 symbols) | Enterprise management disclaimer. Stub no-op. |
| 5 | `ProfileManagementDisclaimerServiceFactory::*` (3 symbols) | same as above |
| 6 | `SigninViewControllerDelegate::CreateSyncHistoryOptInDelegate(...)` | Signin UI delegate. Stub or disable flow. |
| 7 | `SigninViewControllerDelegate::CreateSigninErrorDelegate(...)` | same as above |
| 8 | `SigninViewControllerDelegate::CreateSignoutConfirmationDelegate(...)` | same as above |
| 9 | `SigninViewControllerDelegate::CreateSyncConfirmationDelegate(...)` | same as above |
| 10 | `SigninViewControllerDelegate::CreateProfileCustomizationDelegate(...)` | same as above |
| 11 | `FirstRunService::RegisterLocalStatePrefs(...)` | First-run service. Stub no-op. |
| 12 | `FirstRunServiceFactory::*` (2 symbols) | same as above |
| 13 | `first_run::internal::InitialPrefsPath()` | same as above |
| 14 | `ExternalProtocolHandler::RunExternalProtocolDialog(...)` | External protocol dialog. Stub no-op. |
| 15 | `settings_utils::ShowNetworkProxySettings(...)` | Settings network proxy. Stub no-op. |
| 16 | `upgrade_util::RelaunchChromeBrowserImpl(...)` | Browser relaunch. Stub no-op on QNX. |
| 17 | `HatsNextWebDialog::HatsNextWebDialog(...)` | HaTS survey dialog. Stub no-op. |
| 18 | `PasswordCrossDomainConfirmationPopupControllerImpl::*` (2 symbols) | Password UI popup. Stub no-op. |
| 19 | `ExtensionInstallUIDesktop::ExtensionInstallUIDesktop(...)` | Extension install UI. Stub no-op. |
| 20 | `media_router::MaybeGetWifiSSID(...)` | Media router WiFi SSID. Stub no-op. |
| 21 | `VersionUpdater::Create(...)` | Version updater. Stub no-op. |
| 22 | `FirefoxImporter::FirefoxImporter()` | Firefox importer. Stub no-op. |
| 23 | `GetProfilesINI()` | Profile import helper. Stub no-op. |
| 24 | `SpellingOptionsSubMenuObserver::SpellingOptionsSubMenuObserver(...)` | Spelling submenu. Stub no-op. |
| 25 | `CloseBubbleOnTabActivationHelper::CloseBubbleOnTabActivationHelper(...)` | Bubble helper. Stub no-op. |
| 26 | `autofill::WebauthnDialog::CreateAndShow(...)` | WebAuthn dialog. Stub no-op. |
| 27 | `content_analysis::sdk::Client::Create(...)` | Content analysis SDK. Stub no-op. |

## Category 3: disable feature/caller

These symbols belong to features that are unnecessary for CEF on QNX. The best
fix is to remove the feature from the build using GN flags, or to guard the
caller with `is_qnx` exclusions.

| # | Symbol | Notes |
|---|--------|-------|
| 1 | `enterprise_connectors::DeviceTrustService::GetSignals(...)` | Enterprise device trust. Disable feature. |
| 2 | `enterprise_connectors::DeviceTrustServiceFactory::*` (2 symbols) | same as above |
| 3 | `enterprise_connectors::DeviceTrustConnectorServiceFactory::*` | same as above |
| 4 | `enterprise_signals::SystemSignalsServiceHostFactory::*` (2 symbols) | Enterprise signals. Disable feature. |
| 5 | `enterprise_signals::SignalsAggregatorFactory::*` (2 symbols) | same as above |
| 6 | `enterprise_signals::UserPermissionServiceFactory::GetForProfile(...)` | same as above |
| 7 | `enterprise_idle::IdleTimeoutPolicyHandler::IdleTimeoutPolicyHandler()` | Enterprise idle timeout. Disable. |
| 8 | `enterprise_idle::IdleTimeoutActionsPolicyHandler::IdleTimeoutActionsPolicyHandler(...)` | same as above |
| 9 | `whats_new::CreateWhatsNewRegistry()` | What's New page. Disable. |
| 10 | `RenderViewContextMenu::IsLinkToIsolatedWebApp() const` | Isolated Web Apps. Disable feature. |

---

## Implementation notes

- Category 1 root fixes can often be done by widening `if (is_linux || is_chromeos)`
  to `if (is_linux || is_chromeos || is_qnx)` in the relevant `BUILD.gn` files.
- Category 2 stubs should be placed under `cef/patch/qnx/chromium/new_files/`
  and wired into `BUILD.gn` via CEF-managed patches.
- Category 3 disabling should prefer GN variables (e.g.
  `enable_enterprise_admin_controls=false`) when available; otherwise guard
  callers or source lists with `!is_qnx`.
