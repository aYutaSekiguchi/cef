# QNX: NTP enterprise shortcuts pref constants

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `obj/chrome/browser/browser/new_tab_page_util.o`

## Failure signature

```text
../../chrome/browser/new_tab_page/new_tab_page_util.cc:295:35: error: no member named 'kEnterpriseShortcutsPolicyList' in namespace 'ntp_tiles::prefs'
  295 |       ->GetList(ntp_tiles::prefs::kEnterpriseShortcutsPolicyList)
      |                                   ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

## Root cause

`chrome/browser/new_tab_page/new_tab_page_util.cc` compiles the desktop NTP
enterprise shortcuts code on QNX, but `components/ntp_tiles/pref_names.h`
exposed the enterprise shortcuts pref constants only for Linux, macOS, Windows,
and ChromeOS:

```cpp
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
inline constexpr char kEnterpriseShortcutsPolicyList[] = ...;
inline constexpr char kEnterpriseShortcutsUserList[] = ...;
#endif
```

## Fix

Extend the `ntp_tiles::prefs` enterprise shortcuts pref guard to include
`BUILDFLAG(IS_QNX)`, matching the desktop NTP path used by QNX.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/browser/new_tab_page_util.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "kEnterpriseShortcutsPolicyList|new_tab_page_util|ntp_tiles" docs/qnx/history/build-errors/compile
```
