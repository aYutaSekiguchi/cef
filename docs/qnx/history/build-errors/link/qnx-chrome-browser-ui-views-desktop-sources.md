# QNX chrome/browser/ui desktop Views sources missing from libcef

## Failure signature

Stage: link
Target: `//cef:cefsimple` via `./libcef.so`

Representative unresolved symbols:

```text
./libcef.so: undefined reference to `ProfilePicker::Hide()'
./libcef.so: undefined reference to `ProfilePicker::SwitchToSignIn(...)'
./libcef.so: undefined reference to `IntroUI::IntroUI(content::WebUI*)'
./libcef.so: undefined reference to `settings::SystemHandler::SystemHandler()'
./libcef.so: undefined reference to `BadgedProfilePhoto::BadgedProfilePhoto(...)'
./libcef.so: undefined reference to `FirstRunService::RegisterLocalStatePrefs(...)'
```

## Root cause

`chrome/browser/ui/BUILD.gn` has a large desktop Views/WebUI source block guarded by:

```gn
if (is_win || is_mac || is_linux) {
```

QNX reaches call sites from browser/signin/profile/settings code, but the Views implementations in that block were not compiled into `//chrome/browser/ui:ui` for QNX. Earlier QNX patches only widened a smaller adjacent desktop UI block and the shared `chrome/browser/ui/profiles:impl` profile picker sources.

## Fix

Patch:

```text
cef/patch/patches/qnx/chromium/chrome_browser_ui_views_desktop_sources_qnx.patch
```

Changes:

- Include QNX in the Win/Mac/Linux desktop Views source block.
- Keep unsupported deps out of QNX:
  - `//chrome/enterprise_companion:constants_prod`
  - `//chrome/enterprise_companion:installer_paths`
  - `//chrome/updater:browser_sources`
  - `//components/on_device_translation`
- Keep `webui/on_device_translation_internals/*` sources out of QNX because their mojom/generated headers are not produced when on-device translation is disabled.
- Keep `webui/updater/*` sources and updater deps out of QNX because updater generated headers such as `chrome/updater/updater_version.h` are not produced.
- Remove duplicate QNX-only `profile_customization_bubble_sync_controller` source entries; they are now included by the widened block.

## Verification

`gn --regeneration gen out/qnx_release` succeeds.

`out/qnx_release/obj/chrome/browser/ui/ui.ninja` contains objects for:

```text
profile_picker_view.o
profile_picker_flow_controller.o
profile_picker_reauth_provider.o
first_run_flow_controller.o
intro_ui.o
system_handler.o
```

and does not contain:

```text
on_device_translation_internals_page_handler_impl.o
on_device_translation_internals_ui.o
updater_page_handler.o
updater_ui.o
```

## Search hints

```bash
rg -n "ProfilePicker::Hide|IntroUI::IntroUI|settings::SystemHandler|chrome_browser_ui_views_desktop_sources_qnx" docs/qnx/history/build-errors
```
