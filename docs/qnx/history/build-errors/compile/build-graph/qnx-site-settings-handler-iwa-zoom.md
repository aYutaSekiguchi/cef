# QNX: SiteSettingsHandler IWA zoom platform guard

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `obj/chrome/browser/ui/ui/site_settings_handler.o`

## Failure signature

```text
../../chrome/browser/ui/webui/settings/site_settings_handler.cc:2117:57: error: use of undeclared identifier 'profile_'
 2117 |       content::HostZoomMap::GetDefaultForBrowserContext(profile_);
      |                                                         ^~~~~~~~
../../chrome/browser/ui/webui/settings/site_settings_handler.cc:2125:8: error: cannot define or redeclare 'sort' here because namespace 'settings' does not enclose namespace 'std'
 2125 |   std::sort(zoom_levels.begin(), zoom_levels.end(),
      |   ~~~~~^
../../chrome/browser/ui/webui/settings/site_settings_handler.cc:2173:6: error: use of undeclared identifier 'SiteSettingsHandler'; did you mean 'settings::SiteSettingsHandler'?
```

## Root cause

`SiteSettingsHandler::SendZoomLevels()` has an IWA zoom block shaped like:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
  if (web_app_provider) {
    ... webapps::kIsolatedAppScheme ...
#endif
  }
```

QNX was excluded from the guard, so the opening `if (...) {` was preprocessed
out while the closing `}` remained. That closed the function early and caused
all following statements and method definitions to be parsed at namespace scope.

`HandleRemoveZoomLevel()` has the same isolated-app scheme guard and needs the
same platform treatment.

## Fix

Extend the isolated-app scheme include and the two IWA zoom handling guards to
include `BUILDFLAG(IS_QNX)`.

## Verification

```bash
cd /home/yuta/chromium/test/src
ninja -C out/qnx_release obj/chrome/browser/ui/ui/site_settings_handler.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "SiteSettingsHandler|SendZoomLevels|HandleRemoveZoomLevel|kIsolatedAppScheme" docs/qnx/history/build-errors/compile
```
