# QNX: session_restore_infobar pref guard

## Stage

- stage: compile
- category: cxx / undeclared-identifier
- target: `obj/chrome/browser/ui/views/session_restore_infobar/session_restore_infobar/session_restore_infobar_prefs.o`

## Failure signature

```text
../../chrome/browser/ui/views/session_restore_infobar/session_restore_infobar_prefs.cc:15:7: error: no member named 'kSessionRestoreInfoBarTimesShown' in namespace 'prefs'
  15 |       prefs::kSessionRestoreInfoBarTimesShown,
```

Follow-on errors convert the nearby integer constant
`kSessionRestoreInfoBarMaxTimesToShow` to a `std::string_view` pref path,
because Clang suggests it as the closest name.

## Root cause

`BrowserWindowFeatures` was extended to construct
`SessionRestoreInfobarController` on QNX, which caused the
`session_restore_infobar` target to compile on QNX.

`session_restore_infobar_prefs.cc` references
`prefs::kSessionRestoreInfoBarTimesShown`, but the pref constant in
`chrome/common/pref_names.h` and the registration in
`chrome/browser/ui/browser_ui_prefs.cc` were guarded only for
Win/Mac/Linux:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

QNX was not included, so the pref name was not declared.

## Fix

Extend the pref declaration and registration guards to include
`BUILDFLAG(IS_QNX)`:

- `chrome/common/pref_names.h`
- `chrome/browser/ui/browser_ui_prefs.cc`

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/ui/views/session_restore_infobar/session_restore_infobar/session_restore_infobar_prefs.o
```

Result: `EXIT:0`; no errors emitted.

## Search hints

```bash
rg -n "kSessionRestoreInfoBarTimesShown|session_restore_infobar_prefs" docs/qnx/history/build-errors
```
