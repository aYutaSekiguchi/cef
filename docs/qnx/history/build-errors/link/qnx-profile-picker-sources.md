# QNX profile picker sources missing from libcef

## Failure signature

Stage: link
Target: `//cef:cefsimple` via `./libcef.so`

Representative unresolved symbols:

```text
./libcef.so: undefined reference to `ProfilePicker::Shown()'
./libcef.so: undefined reference to `ProfilePicker::Hide()'
./libcef.so: undefined reference to `ProfilePicker::SwitchToSignIn(...)'
./libcef.so: undefined reference to `ProfilePicker::Params::~Params()'
./libcef.so: undefined reference to `ProfilePicker::GetStartupMode()'
./libcef.so: undefined reference to `ProfilePicker::PickProfile(...)'
./libcef.so: undefined reference to `ProfilePicker::Show(ProfilePicker::Params&&)'
```

## Root cause

`ProfilePicker` is split across multiple desktop UI implementation files:

- `chrome/browser/ui/profiles/profile_picker.cc` defines shared state, params helpers, `Shown()`, `GetStartupMode()`, etc.
- `chrome/browser/ui/views/profiles/profile_picker_view.cc` and related Views files define the UI flow (`Show()`, `Hide()`, sign-in/reauth switches, etc.).

QNX reaches desktop profile picker call sites from browser/signin UI code, but `chrome/browser/ui/profiles/BUILD.gn` only compiled the shared `profile_picker.cc` source for Win/Mac/Linux.

## Fix

Patch:

```text
cef/patch/patches/qnx/chromium/profile_picker_profiles_sources_qnx.patch
```

Change:

```gn
-  if (is_win || is_mac || is_linux) {
+  if (is_win || is_mac || is_linux || is_qnx) {
```

This includes:

```text
profile_customization_synced_theme_waiter.cc
profile_customization_util.cc
profile_picker.cc
```

for QNX.

## Notes

This fixes the shared profile-picker implementation symbols. If Views-specific `ProfilePicker::Show()`, `Hide()`, or sign-in flow symbols remain unresolved, verify that the QNX desktop UI patch for `chrome/browser/ui/BUILD.gn` is applied and that `profile_picker_view.cc` and related `views/profiles/profile_picker_*` objects are present in `libcef.ninja`.

## Search hints

```bash
rg -n "ProfilePicker::Shown|ProfilePicker::Params::~Params|profile_picker_profiles_sources_qnx|profile_picker.cc" docs/qnx/history/build-errors
```
