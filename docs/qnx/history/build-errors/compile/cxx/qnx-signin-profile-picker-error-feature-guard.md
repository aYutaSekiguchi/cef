# QNX signin profile picker error feature

## Failure signature

Stage: compile
Target: `//chrome/browser/ui:ui`
Object: `profile_picker_flow_controller.o`

```text
chrome/browser/ui/views/profiles/profile_picker_flow_controller.cc:703:46:
error: no member named 'kSupportErrorsInProfilePicker' in namespace 'switches'
```

## Root cause

`profile_picker_flow_controller.cc` is built for QNX through the desktop UI Views block, but `kSupportErrorsInProfilePicker` is declared and defined only for Win/Mac/Linux in `components/signin/public/base/signin_switches.{h,cc}`.

## Fix

Patch:

```text
cef/patch/patches/qnx/chromium/signin_switches_profile_picker_qnx.patch
```

Widen the platform guard for both the declaration and definition to include QNX:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_QNX)
```

## Verification

```bash
autoninja -C out/qnx_release obj/chrome/browser/ui/ui/profile_picker_flow_controller.o
```

The object builds successfully.

## Search hints

```bash
rg -n "kSupportErrorsInProfilePicker|signin_switches_profile_picker_qnx|profile_picker_flow_controller" docs/qnx/history/build-errors
```
