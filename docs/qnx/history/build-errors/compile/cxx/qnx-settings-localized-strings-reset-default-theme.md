# QNX: SettingsLocalizedStringsProvider resetToDefaultTheme guard

## Stage

- stage: compile
- category: cxx / undeclared-identifier
- target: `obj/chrome/browser/ui/ui/settings_localized_strings_provider.o`

## Failure signature

```text
../../chrome/browser/ui/webui/settings/settings_localized_strings_provider.cc:549:31: error: use of undeclared identifier 'IDS_SETTINGS_RESET_TO_DEFAULT_THEME'
  549 |       {"resetToDefaultTheme", IDS_SETTINGS_RESET_TO_DEFAULT_THEME},
../../chrome/browser/ui/webui/settings/settings_localized_strings_provider.cc:562:16: error: no matching member function for call to 'AddLocalizedStrings'
```

The follow-on `AddLocalizedStrings` error is a consequence: the
`kLocalizedStrings` array of `{const char*, int}` pairs has a non-int
entry (the undeclared identifier) so its element type is not deducible
and the call cannot pick a candidate overload.

## Root cause

`chrome/app/settings_strings.grdp` declares
`IDS_SETTINGS_RESET_TO_DEFAULT_THEME` under:

```xml
<if expr="not is_linux">
```

QNX is grit-mapped to `target_platform = "linux"` in
`tools/grit/grit_args.gni` (added in
`grit_args_qnx_linux_fallback.patch`), so `is_linux` evaluates true on
QNX, the `<if expr="not is_linux">` block is skipped, and the string
identifier is never generated.

`settings_localized_strings_provider.cc` references the identifier
under a different condition:

```cpp
#if !BUILDFLAG(IS_LINUX)
      {"resetToDefaultTheme", IDS_SETTINGS_RESET_TO_DEFAULT_THEME},
#endif
```

`BUILDFLAG(IS_LINUX)` is `false` on QNX (QNX is its own BUILDFLAG), so
the .cc attempts to emit the entry on QNX, even though the grdp does
not generate the ID. This produces the undeclared-identifier error.

## Fix

Tighten the .cc condition to match the actual grdp-emitted set:
`BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_CHROMEOS)`. The
grdp is left unchanged because the platform-agnostic
`is_win or is_macosx or is_chromeos` re-expression cannot be evaluated
by grit (grit only knows the literal set of
`is_linux/is_macosx/is_win/is_chromeos/...`) and would risk further
breakage.

The downstream effect is consistent: the string is only generated and
only referenced on Win, Mac, and ChromeOS, matching the original
grdp intent.

## Verification

Clean bootstrap (full reset of test tree + `gclient sync` + CEF copy +
`qnx_sync_sources.sh` + `cef_create_projects_qnx.sh`):

```bash
./tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root $QNX_SDP
```

Narrow build of the originally failing object:

```bash
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release obj/chrome/browser/ui/ui/settings_localized_strings_provider.o
```

Result: `EXIT:0`; no errors emitted.

## Search hints

```bash
rg -n "IDS_SETTINGS_RESET_TO_DEFAULT_THEME|resetToDefaultTheme" docs/qnx/history/build-errors
```
