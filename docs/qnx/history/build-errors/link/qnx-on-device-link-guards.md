# QNX disable remaining on-device model link references

## Summary

After `enable_on_device_model=false` on QNX, most Dawn/on-device model deps are
removed, but `libcef.so` still linked objects that referenced disabled
on-device symbols:

```text
on_device_internals::OnDeviceInternalsUI::kWebUIControllerType
on_device_internals::OnDeviceInternalsUI::BindInterface(...PageHandlerFactory)
on_device_model::PreSandboxInit()
on_device_model::Shutdown()
on_device_model::OnDeviceModelService::Create(...)
```

## Root cause

The GN guards in `enable_on_device_model_qnx.patch` removed the primary deps,
but a few desktop and utility registration paths were still unconditional:

- `chrome/browser/ui/webui/chrome_web_ui_configs.cc` registered
  `OnDeviceInternalsUIConfig`.
- `chrome/browser/chrome_browser_interface_binders_webui_parts_desktop.cc`
  registered the on-device internals WebUI binder.
- `content/utility/utility_main.cc` called on-device model pre-sandbox/shutdown
  helpers.
- `content/utility/services.cc` registered `RunOnDeviceModel`.
- `chrome/browser/resources:dev_ui_resources` still included on-device internals
  resources.
- `chrome/browser:browser` and `browser_generated_files` still pulled
  on-device model mojom/on-device internals mojom targets.

## Fix

Patch:

```text
cef/patch/patches/qnx/chromium/on_device_link_guards_qnx.patch
```

Add `!BUILDFLAG(IS_QNX)` / `!is_qnx` guards around those residual registration
and generated-resource paths. This keeps non-QNX behavior unchanged while
matching the QNX `enable_on_device_model=false` build graph.

## Verification

```bash
source out/qnx_release/qnx_env.sh
buildtools/linux64/gn --root=. -q --regeneration gen out/qnx_release
autoninja -C out/qnx_release cefsimple
```

Observed `GN_EXIT:0`; `grep 'undefined reference' /tmp/cefsimple_build.log |
grep -E 'on_device|OnDevice'` returned no lines. The total distinct undefined
references dropped from 106 to 103. The remaining link failures belong to other
platform groups (`payments`, `views`, `syncer`, `enterprise`, `platform_util`,
`crashpad`, etc.).

## Search hints

```bash
rg -n "OnDeviceInternalsUI|OnDeviceModelService::Create|PreSandboxInit|on_device_link_guards" docs/qnx/history/build-errors
```
