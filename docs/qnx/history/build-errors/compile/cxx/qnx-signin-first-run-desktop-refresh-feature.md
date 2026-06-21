# QNX signin first run desktop refresh

## Failure signature

Stage: compile
Target: `//chrome/browser/ui:webui_signin_signin_impl`
Object: `history_sync_optin_ui.o`

```text
chrome/browser/ui/webui/signin/history_sync_optin/history_sync_optin_ui.cc:91:17:
error: no member named 'IsFirstRunDesktopRefreshEnabled' in namespace 'switches'
```

Also propagates to `signin_switches.cc` itself:

```text
components/signin/public/base/signin_switches.cc:414:30:
error: use of undeclared identifier 'FirstRunDesktopSignInPromoVariation'
```

## Root cause

After the desktop UI Views block widened to QNX, several desktop signin WebUIs
(`history_sync_optin_ui.cc`, `intro_ui.cc`, `profile_customization_ui.cc`,
`profile_picker_ui.cc`) call `switches::IsFirstRunDesktopRefreshEnabled()`.

The function declaration lives outside any platform guard in
`components/signin/public/base/signin_switches.h`, but its definition and the
`FirstRunDesktopSignInPromoVariation` enum it depends on are gated to
`IS_WIN||IS_MAC||IS_LINUX` in both the header and the source.

## Fix

Patch:

```text
cef/patch/patches/qnx/chromium/signin_switches_first_run_desktop_refresh_qnx.patch
```

Widen the platform guard to include QNX in three places:

- The `kFirstRunDesktopRefresh`, `kFirstRunDesktopChoiceScreenRefresh`,
  `IsFirstRunDesktopRefreshEnabled()` and `kFirstRunDesktopSignInPromoVariation`
  block in `signin_switches.h`.
- The same block in `signin_switches.cc`.
- The closing `#endif` comments.

## Verification

```bash
autoninja -C out/qnx_release \
  obj/chrome/browser/ui/webui/signin/signin_impl/history_sync_optin_ui.o
```

The object builds successfully.

## Search hints

```bash
rg -n "IsFirstRunDesktopRefreshEnabled|FirstRunDesktopSignInPromoVariation|signin_switches_first_run_desktop_refresh_qnx" docs/qnx/history/build-errors
```
