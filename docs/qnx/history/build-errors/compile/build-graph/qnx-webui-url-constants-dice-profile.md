# QNX: webui_url_constants DICE profile URLs guard

## Stage

- stage: compile
- category: build-graph / missing-constant
- file: `chrome/common/webui_url_constants.h`
- downstream: `chrome/browser/ui/views/profiles/signin_view_controller_delegate_views.cc`

## Failure signature

```text
In file included from .../signin_view_controller_delegate_views.cc:5:
In file included from .../signin_view_controller_delegate_views.h:14:
../../chrome/browser/ui/webui/signin/managed_user_profile_notice_ui.h:38:36: error: no member named 'kChromeUIManagedUserProfileNoticeHost' in namespace 'chrome'
   38 |                            chrome::kChromeUIManagedUserProfileNoticeHost) {}
../../chrome/browser/ui/webui/signin/profile_customization_ui.h:36:36: error: no member named 'kChromeUIProfileCustomizationHost' in namespace 'chrome'
   36 |                            chrome::kChromeUIProfileCustomizationHost) {}
../../chrome/browser/ui/views/profiles/signin_view_controller_delegate_views.cc:191:27: error: no member named 'kChromeUIProfileCustomizationURL' in namespace 'chrome'
3 errors generated.
```

## Root cause

The signin/webui host constants used by profile-customization and
managed-user-profile-notice pages live inside a guard:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

The same guard also protects `kChromeUIDefaultBrowserModalHost/URL`,
`kChromeUIWebAppSettingsHost/URL`, `kChromeUIWhatsNewHost/URL`, the `intro`
block, and `kChromeUIBrowserSwitchHost/URL` (lines around 437-484 in
`chrome/common/webui_url_constants.h`).

QNX desktop builds follow the same desktop signin path, but the guard
excludes QNX. Several signin views include these constants directly
(through `managed_user_profile_notice_ui.h` and
`profile_customization_ui.h`), so the references fail before the
C++ standard guards can suppress the call sites.

## Fix

Extend the guard to allow QNX. The first guard in the file
(DefaultBrowserModal / WebAppSettings / WhatsNew) is already covered by
`webui_url_constants_whatsnew_qnx`; this patch only needs to extend the
*second* guard, which begins with the `kChromeUIBrowserSwitchHost` /
`kChromeUIIntro*` / `kChromeUIManagedUserProfileNotice*` /
`kChromeUIProfileCustomization*` / `kChromeUIProfilePicker*` /
`kChromeUIHistorySyncOptin*` / `kChromeUIUpdater*` block:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_QNX)
inline constexpr char kChromeUIBrowserSwitchHost[] = "browser-switch";
...
inline constexpr char kChromeUIUpdaterURL[] = "chrome://updater/";
#endif
```

This mirrors the rationale used by `webui_url_constants_whatsnew_qnx`.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/ui/ui/signin_view_controller_delegate_views.o
```

Result: `signin_view_controller_delegate_views.o` compiles successfully.

## Search hints

```bash
rg -n "kChromeUIProfileCustomization|kChromeUIManagedUserProfileNotice|webui_url_constants|signin_view_controller_delegate_views" docs/qnx/history/build-errors/compile
```
