# QNX: SigninViewController::ShowModalHistorySyncOptInDialog guard

## Stage

- stage: compile
- category: build-graph / missing-symbol
- files:
  - `chrome/browser/ui/signin/signin_view_controller.h`
  - `chrome/browser/ui/signin/signin_view_controller.cc`
- downstream:
  - `chrome/browser/ui/webui/signin/history_sync_optin_service.cc`
  - `chrome/browser/ui/views/profiles/signin_view_controller_delegate_views.cc`

## Failure signature

```text
../../chrome/browser/ui/webui/signin/history_sync_optin_service.cc:52:9: error: no member named 'ShowModalHistorySyncOptInDialog' in 'SigninViewController'
   52 |       ->ShowModalHistorySyncOptInDialog(/*should_close_modal_dialog=*/true,
      |         ^
1 error generated.
```

## Root cause

`SigninViewController::ShowModalHistorySyncOptInDialog` and the related
history-sync opt-in delegate/view helpers are declared and defined under:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

`history_sync_optin_service.cc` (DICE history-sync opt-in flow) calls the
controller method unconditionally, and `CreateSyncHistoryOptInDelegate` calls
`SigninViewControllerDelegateViews::CreateHistorySyncOptInWebView`. QNX is not
in those platform expressions, so call sites failed to resolve.

## Fix

Extend the controller, delegate, view-helper, and `HistorySyncOptinUI` include
guards to allow QNX:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_QNX)
```

This mirrors the rationale used by `signin_util_qnx` and
`web_app_install_shortcut_and_settings_qnx`. When verifying only the narrow
object, build `//chrome/browser/ui/webui/signin/history_sync_optin:mojo_bindings`
first so `history_sync_optin.mojom.h` exists; the full browser target carries
that dependency.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . \
  chrome/browser/ui/webui/signin/history_sync_optin:mojo_bindings
ninja -C . \
  obj/chrome/browser/ui/webui/signin/signin_impl/history_sync_optin_service.o
ninja -C . \
  obj/chrome/browser/ui/ui/signin_view_controller_delegate_views.o
```

Result: both object builds compile successfully.

## Search hints

```bash
rg -n "ShowModalHistorySyncOptInDialog|signin_view_controller|history_sync_optin_service" docs/qnx/history/build-errors/compile
```
