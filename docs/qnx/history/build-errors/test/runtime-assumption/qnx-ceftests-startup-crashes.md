# QNX ceftests startup crashes during CefInitialize

## Failure signatures

Stage: test
Category: runtime-assumption / platform-stub
Target: `//cef:ceftests`

After QNX `ceftests` linked, even metadata-only commands crashed before listing tests:

```text
./ceftests --gtest_list_tests
segmentation violation (core dumped)
exit 139
```

The first core showed a null `AudioManager`:

```text
#0 media::AudioManager::Create() at media/audio/audio_manager.cc:114
#1 content::BrowserMainLoop::InitializeAudio()
#2 content::BrowserMainRunnerImpl::Initialize()
#3 CefInitialize()
```

After replacing the null audio stub, the next cores exposed additional null platform stubs:

```text
#0 KeyedServiceBaseFactory::type()
#1 KeyedServiceBaseFactory::DependsOn()
#2 enterprise_reporting::CloudProfileReportingServiceFactory::CloudProfileReportingServiceFactory()
#3 enterprise_reporting::CloudProfileReportingServiceFactory::GetInstance()
#4 ChromeBrowserMainExtraPartsProfiles::EnsureBrowserContextKeyedServiceFactoriesBuilt()
```

and then:

```text
#0 NotificationPlatformBridgeDelegator::GetDisplayed()
#1 NotificationDisplayServiceImpl::GetDisplayed()
#2 PlatformNotificationServiceImpl::GetDisplayedNotifications()
```

followed by:

```text
#0 NotificationPlatformBridgeDelegator::Close()
#1 BackgroundContentsService::CloseBalloon()
#2 BackgroundContentsService::OnExtensionLoaded()
```

## Root cause

Several QNX bring-up stubs returned `nullptr` for objects that Chromium's browser startup assumes are either absent behind a platform guard or represented by a usable no-op implementation:

- `media/audio/audio_manager_qnx.cc::CreateAudioManager()` returned `nullptr`; `AudioManager::Create()` dereferenced it unconditionally.
- `enterprise_signals::SignalsAggregatorFactory::GetInstance()` is a QNX stub returning `nullptr`; `CloudProfileReportingServiceFactory` called `DependsOn()` on it unconditionally.
- `NotificationPlatformBridge::Create()` returned `nullptr`, and `CanHandleType()` returned false. On QNX the message-center bridge is also unavailable, so `NotificationPlatformBridgeDelegator` later dereferenced a null bridge in `GetDisplayed()` / `Close()`.

## Fix

- Use `FakeAudioManager` for QNX audio manager creation.
- Guard the cloud profile reporting dependency on `SignalsAggregatorFactory` out on QNX.
- Replace the QNX notification bridge null stub with a no-op `NotificationPlatformBridgeQnx` and report `CanHandleType() == true` so callers use the no-op bridge instead of falling through to an absent message-center bridge.

Files / patch stack:

- `cef/patch/qnx/chromium/new_files/media/audio/audio_manager_qnx.cc`
- `cef/patch/patches/qnx/chromium/chrome_browser_cloud_profile_reporting_qnx.patch`
- `cef/patch/qnx/chromium/new_files/chrome/browser/notifications/notification_platform_bridge_linux_qnx.cc`
- `cef/patch/patch.cfg`

## Verification

```bash
./out/qnx_release/ninja_qnx.sh ceftests
./cef/tools/qnx_run_test.sh --cmd \
  'export CHROME_EXE_PATH=/mnt/nfs/out/qnx_release/ceftests; ./ceftests --gtest_list_tests' \
  --timeout 300 --kill-existing
./cef/tools/qnx_run_test.sh --ceftests 'VersionTest.*' --timeout 300 --kill-existing
```

Results:

- `ceftests --gtest_list_tests` exits 0 and no core file is produced.
- `VersionTest.*` runs 2 tests and passes 2/2.

GL/EGL initialization errors are still printed by the GPU process during startup, but they do not crash these smoke checks.
