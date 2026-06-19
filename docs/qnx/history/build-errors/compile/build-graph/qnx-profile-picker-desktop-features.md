# QNX profile picker desktop feature and regional capability guards

## Failure signature

Stage: compile
Category: build-graph
Targets:

- `obj/chrome/browser/ui/webui/signin/profile_impl/profile_customization_ui.o`
- `obj/chrome/browser/ui/webui/signin/profile_impl/profile_picker_ui.o`
- `obj/chrome/browser/ui/webui/signin/profile_impl/profile_picker_handler.o`

Primary diagnostics:

```text
profile_customization_ui.cc:127:49: error: no member named 'IsFirstRunDesktopRefreshEnabled' in namespace 'switches'
profile_picker_ui.cc:324:21: error: no member named 'kOpenAllProfilesFromProfilePickerExperiment' in namespace 'switches'
profile_picker_ui.cc:327:17: error: no member named 'kMaxProfilesCountToShowOpenAllButtonInProfilePicker' in namespace 'switches'
profile_picker_ui.cc:364:12: error: no member named 'IsInSearchEngineChoiceScreenRegionForSystemProfile'
profile_picker_handler.cc:501:17: error: no member named 'kOpenAllProfilesFromProfilePickerExperiment' in namespace 'switches'
```

After exposing the factory helper, the regional service object can also report:

```text
regional_capabilities_service_factory.cc:73:10: error: no matching function for call to 'IsInSearchEngineChoiceScreenRegion'
```

## Root cause

QNX builds the desktop profile picker/customization WebUIs. Those files use desktop first-run refresh/profile-picker feature switches and the system-profile regional capabilities helper.

The relevant declarations/definitions were guarded for Win/Mac/Linux only:

- `switches::kFirstRunDesktopRefresh`
- `switches::kFirstRunDesktopChoiceScreenRefresh`
- `switches::IsFirstRunDesktopRefreshEnabled()`
- `switches::kOpenAllProfilesFromProfilePickerExperiment`
- `switches::kMaxProfilesCountToShowOpenAllButtonInProfilePicker`
- `RegionalCapabilitiesServiceFactory::IsInSearchEngineChoiceScreenRegionForSystemProfile()`
- `RegionalCapabilitiesService::IsInSearchEngineChoiceScreenRegion(Client&)`

## Fix

Patch: `cef/patch/patches/qnx/chromium/profile_picker_desktop_features_qnx.patch`

Changes:

- Add `BUILDFLAG(IS_QNX)` to the Win/Mac/Linux guards around the desktop first-run refresh and open-all-profiles profile picker switches in:
  - `components/signin/public/base/signin_switches.h`
  - `components/signin/public/base/signin_switches.cc`
- Add `BUILDFLAG(IS_QNX)` to the system-profile regional capability helper guards in:
  - `chrome/browser/regional_capabilities/regional_capabilities_service_factory.h`
  - `chrome/browser/regional_capabilities/regional_capabilities_service_factory.cc`
  - `components/regional_capabilities/regional_capabilities_service.h`
  - `components/regional_capabilities/regional_capabilities_service.cc`

## Verification

```text
./out/qnx_release/ninja_qnx.sh \
  obj/components/signin/public/base/signin_switches/signin_switches.o \
  obj/components/regional_capabilities/regional_capabilities/regional_capabilities_service.o \
  obj/chrome/browser/regional_capabilities/regional_capabilities/regional_capabilities_service_factory.o \
  obj/chrome/browser/ui/webui/signin/profile_impl/profile_customization_ui.o \
  obj/chrome/browser/ui/webui/signin/profile_impl/profile_picker_ui.o \
  obj/chrome/browser/ui/webui/signin/profile_impl/profile_picker_handler.o
EXIT:0
```

Patch state check on the patched tree:

```text
patch -p0 --reverse --dry-run < cef/patch/patches/qnx/chromium/profile_picker_desktop_features_qnx.patch
checking file chrome/browser/regional_capabilities/regional_capabilities_service_factory.cc
checking file chrome/browser/regional_capabilities/regional_capabilities_service_factory.h
checking file components/regional_capabilities/regional_capabilities_service.cc
checking file components/regional_capabilities/regional_capabilities_service.h
checking file components/signin/public/base/signin_switches.cc
checking file components/signin/public/base/signin_switches.h
rc=0
```
