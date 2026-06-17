# QNX: SigninViewController::ShowModalHistorySyncOptInDialog guard

## Stage

- stage: compile
- category: build-graph / missing-symbol
- files:
  - `chrome/browser/ui/signin/signin_view_controller.h`
  - `chrome/browser/ui/signin/signin_view_controller.cc`
- downstream: `chrome/browser/ui/webui/signin/history_sync_optin_service.cc`

## Failure signature

```text
../../chrome/browser/ui/webui/signin/history_sync_optin_service.cc:52:9: error: no member named 'ShowModalHistorySyncOptInDialog' in 'SigninViewController'
   52 |       ->ShowModalHistorySyncOptInDialog(/*should_close_modal_dialog=*/true,
      |         ^
1 error generated.
```

## Root cause

`SigninViewController::ShowModalHistorySyncOptInDialog` is declared and
defined under:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

`history_sync_optin_service.cc` (DICE history-sync opt-in flow) calls the
method unconditionally. QNX is not in the platform expression, so the
call site failed to resolve.

## Fix

Extend both the declaration and the definition guards to allow QNX:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_QNX)
```

This mirrors the rationale used by `signin_util_qnx` and
`web_app_install_shortcut_and_settings_qnx`.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . \
  obj/chrome/browser/ui/webui/signin/signin_impl/history_sync_optin_service.o
```

Result: `history_sync_optin_service.o` compiles successfully.

## Search hints

```bash
rg -n "ShowModalHistorySyncOptInDialog|signin_view_controller|history_sync_optin_service" docs/qnx/history/build-errors/compile
```
