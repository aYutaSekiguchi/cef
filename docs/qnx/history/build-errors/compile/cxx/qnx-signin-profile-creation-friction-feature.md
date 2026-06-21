# QNX signin profile creation friction feature

## Failure signature

Stage: compile
Target: `//chrome/browser/ui:webui_signin_profile_impl`
Object: `profile_customization_ui.o`

```text
chrome/browser/ui/webui/signin/profile_customization_ui.cc:121:15:
error: no member named 'kProfileCreationFrictionReductionExperimentPrefillNameRequirement'
in namespace 'switches'
```

## Root cause

`profile_customization_ui.cc` is built for QNX through the desktop UI Views
block. It references desktop signin features including
`kProfileCreationFrictionReductionExperimentPrefillNameRequirement`,
`kPasswordUploadUiUpdate`, `kProfileCreationDeclineSigninCTAExperiment`,
`kProfileCreationFrictionReductionExperimentRemoveSigninStep`,
`kProfileCreationFrictionReductionExperimentSkipCustomizeProfile`,
`kProfilePickerTextVariations`, and the `ProfilePickerVariation` enum.

All of these are declared and defined inside an
`IS_WIN||IS_MAC||IS_LINUX` guard in
`components/signin/public/base/signin_switches.{h,cc}`.

## Fix

Patch:

```text
cef/patch/patches/qnx/chromium/signin_switches_profile_creation_friction_qnx.patch
```

Widen the platform guard to include QNX in both the header and the source.

## Verification

```bash
autoninja -C out/qnx_release \
  obj/chrome/browser/ui/webui/signin/profile_impl/profile_customization_ui.o
```

The object builds successfully.

## Search hints

```bash
rg -n "kProfileCreationFrictionReductionExperimentPrefillNameRequirement|signin_switches_profile_creation_friction_qnx|profile_customization_ui" docs/qnx/history/build-errors
```
