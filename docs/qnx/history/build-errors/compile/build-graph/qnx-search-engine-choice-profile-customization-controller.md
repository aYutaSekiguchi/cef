# QNX: SearchEngineChoice profile customization controller

## Stage

- stage: compile
- category: build-graph / platform guard
- target: `obj/chrome/browser/search_engine_choice/impl/search_engine_choice_dialog_service.o`

## Failure signature

```text
../../chrome/browser/search_engine_choice/search_engine_choice_dialog_service.cc:383:24: error: no member named 'profile_customization_bubble_sync_controller' in 'BrowserWindowFeatures'
  383 |       browser_features.profile_customization_bubble_sync_controller()
```

## Root cause

`search_engine_choice_dialog_service.cc` suppresses the search-engine
choice dialog while other profile/sign-in UI is pending. It checks
`BrowserWindowFeatures::profile_customization_bubble_sync_controller()`
under `!BUILDFLAG(IS_CHROMEOS)`, so QNX reaches that code path.

`BrowserWindowFeatures` only forward-declares, exposes, stores, and
constructs `ProfileCustomizationBubbleSyncController` under:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

QNX is not in that guard set, so the accessor is not declared.

The controller implementation source is also normally part of the large
`is_win || is_mac || is_linux` block in `chrome/browser/ui/BUILD.gn`.
That block cannot simply be widened to QNX because it also pulls in
on-device-translation internals and `//components/on_device_translation`,
which assert when `enable_on_device_translation=false` (QNX default).

## Fix

- Extend the relevant `BrowserWindowFeatures` guards to include
  `BUILDFLAG(IS_QNX)`:
  - forward declarations in `browser_window_features.h`
  - accessor in `browser_window_features.h`
  - member fields in `browser_window_features.h`
  - `session_restore_infobar_controller.h` include in
    `browser_window_features.cc`
  - construction block in `BrowserWindowFeatures::Init()`
- Add the `session_restore_infobar` dep for QNX in:
  - `chrome/browser/ui/browser_window/internal/BUILD.gn`
  - `chrome/browser/ui/startup/BUILD.gn`
- Add a QNX-only `chrome/browser/ui/BUILD.gn` block for just
  `profile_customization_bubble_sync_controller.{cc,h}` and the deps it
  needs, instead of widening the large desktop block that also pulls in
  unsupported on-device-translation targets.

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/search_engine_choice/impl/search_engine_choice_dialog_service.o
```

Result: `EXIT:0`; no errors emitted.

## Search hints

```bash
rg -n "profile_customization_bubble_sync_controller|SearchEngineChoice|BrowserWindowFeatures" docs/qnx/history/build-errors
```
