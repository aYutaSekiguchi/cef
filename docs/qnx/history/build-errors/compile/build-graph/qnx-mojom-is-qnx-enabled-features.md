# QNX: `screen_ai::mojom::ScreenAIServiceFactory` has no `kServiceSandbox` member

- Date: 2026-06-15
- Signature: `no member named 'kServiceSandbox' in 'screen_ai::mojom::ScreenAIServiceFactory'`, `static assertion failed: This interface does not declare a proper ServiceSandbox attribute`
- Stage: compile
- Category: build-graph
- Scope: `services/screen_ai/public/mojom/screen_ai_factory.mojom`,
  `services/screen_ai/public/mojom/screen_ai_service.mojom`,
  `sandbox/policy/mojom/sandbox.mojom`,
  `mojo/public/tools/bindings/mojom.gni`

## Symptoms

- After applying the `font_platform_data_qnx_dsf_fallback` fix, the wider
  `cefsimple` build progressed to step 1665/39567 and failed on
  `obj/chrome/browser/screen_ai/screen_ai_service_router_factory/screen_ai_service_handler_base.o`:
  ```
  ../../content/public/browser/service_process_host.h:45:51: error: no member named 'kServiceSandbox' in 'screen_ai::mojom::ScreenAIServiceFactory'
    45 |   using ProvidedSandboxType = decltype(Interface::kServiceSandbox);
  ../../content/public/browser/service_process_host.h:47:7: error: static assertion failed: This interface does not declare a proper ServiceSandbox attribute. See //docs/mojo_and_services.md (Specifying a sandbox).
  ../../content/public/browser/service_process_host.h:51:21: error: no member named 'kServiceSandbox' in 'screen_ai::mojom::ScreenAIServiceFactory'
  ```
- The screen_ai mojo files have the right attribute:
  ```
  // services/screen_ai/public/mojom/screen_ai_factory.mojom
  [ServiceSandbox=sandbox.mojom.Sandbox.kScreenAI]
  interface ScreenAIServiceFactory { ... }
  ```
- But the generated `gen/services/screen_ai/public/mojom/screen_ai_factory.mojom.h`
  is missing the `kServiceSandbox` static member. The `ScreenAIServiceFactoryInterfaceBase`
  class is emitted as `class ScreenAIServiceFactoryInterfaceBase {};` (empty).

## Root cause — two-stage filter

The mojo binding pipeline has **two** filters that must both accept `is_qnx`
before the C++ binding for `kScreenAI` is generated:

### Stage 1: AST filter (parser)

`mojo/public/tools/mojom/mojom/parse/conditional_features.py::_IsEnabled()`
evaluates each `[EnableIf=...]` against the `enabled_features` set that the
parser was invoked with. The set is built by
`mojo/public/tools/bindings/mojom.gni:725-749`:

```python
if (is_android) {
  enabled_features += [ "is_android" ]
} else if (is_chromeos) {
  enabled_features += [ "is_chromeos" ]
} else if (is_fuchsia) {
  enabled_features += [ "is_fuchsia" ]
} else if (is_ios) {
  enabled_features += [ "is_ios" ]
} else if (is_linux) {
  enabled_features += [ "is_linux" ]
} else if (is_mac) {
  enabled_features += [ "is_mac" ]
} else if (is_win) {
  enabled_features += [ "is_win" ]
}
```

`is_qnx` is **not** in the list. As a result, on QNX the parser's
`RemoveDisabledDefinitions` step drops every definition whose `EnableIf` includes
`is_qnx` *before* the AST reaches the code generator. Most relevant here:
`kScreenAI` in `sandbox/policy/mojom/sandbox.mojom`:
```mojom
[EnableIf=is_chromeos|is_linux|is_mac|is_win] kScreenAI,
```
is silently filtered out, so the generated `sandbox.mojom-shared.h` does not
contain `kScreenAI` in the `Sandbox` enum.

### Stage 2: template gate (generator)

The C++ template `mojo/public/tools/bindings/generators/cpp_templates/interface_declaration.tmpl:36-38`
emits `kServiceSandbox` only when `interface.service_sandbox` is truthy:

```jinja
{%- if interface.service_sandbox %}
{%- set sandbox_enum = "%s"|format(interface.service_sandbox.GetSpec()|replace(".","::")) %}
  static constexpr auto kServiceSandbox = {{ sandbox_enum }};
{%- endif %}
```

`interface.service_sandbox` (defined in
`mojo/public/tools/mojom/mojom/generate/module.py:1176-1188`) reads the
`ServiceSandbox` attribute from the translated interface and resolves the
right-hand side via `_MapAttributeValue` →
`_LookupValue(module, None, None, "sandbox.mojom.Sandbox.kScreenAI")`. If the
value cannot be resolved to an `EnumValue` (because Stage 1 filtered the enum
value out of the `sandbox.mojom` module's `kinds` dict), the translation
returns the bare identifier string. The `service_sandbox` property then sees
neither `None` (the absence indicator) nor an `EnumValue` and either raises or
returns `None`, and the template emits nothing.

### Combined effect

On QNX, with the existing patches already applied:
- `enable_screen_ai_service = true` (from `screen_ai_features_qnx_service.patch`)
- The screen_ai mojo files are compiled
- The mojo parser receives `enabled_features = ["is_posix"]` (QNX is POSIX, but
  no `is_qnx` entry)
- `kScreenAI` is filtered out of the parsed sandbox.mojom AST
- The generator cannot resolve `Sandbox.kScreenAI` for `ScreenAIServiceFactory`'s
  `ServiceSandbox` attribute
- The C++ template drops the `kServiceSandbox` line
- Every consumer of `screen_ai::mojom::ScreenAIServiceFactory` (and the
  shutdown-handler interface, which has the same attribute) fails to compile

## Fix pattern

Two coordinated additions, one in each filter stage:

1. **`sandbox/policy/mojom/sandbox.mojom:90`** — add `is_qnx` to the EnableIf of
   `kScreenAI` so the enum value is emitted when `is_qnx` is true. This is a
   single-line change in the source `.mojom`.

2. **`mojo/public/tools/bindings/mojom.gni`** — teach the parser's
   `enabled_features` list about `is_qnx`. Add the platform as the
   `else if (is_qnx) { enabled_features += [ "is_qnx" ] }` branch at the same
   priority as the other platform branches. This is a 2-line addition.

Both fixes are needed: only fixing Stage 1 (sandbox.mojom) leaves Stage 2 still
filtering the enum value out at parse time, and the C++ bindings still drop
`kServiceSandbox`. Only fixing Stage 2 (mojom.gni) is a no-op because the source
`.mojom` doesn't even mention `is_qnx`.

## Applied change

Two new CEF-managed patches in `cef/patch/patches/qnx/chromium/`:

### `sandbox_mojom_kScreenAI_qnx.patch`

```diff
--- a/sandbox/policy/mojom/sandbox.mojom
+++ b/sandbox/policy/mojom/sandbox.mojom
@@ -87,7 +87,7 @@ enum Sandbox {
   [EnableIf=is_chromeos|is_linux|is_mac|is_win] kPrintBackend,
 
   // Like kUtility but allows loading of screen AI library.
-  [EnableIf=is_chromeos|is_linux|is_mac|is_win] kScreenAI,
+  [EnableIf=is_chromeos|is_linux|is_qnx|is_mac|is_win] kScreenAI,
```

### `mojom_gni_is_qnx_enabled_features.patch`

```diff
--- a/mojo/public/tools/bindings/mojom.gni
+++ b/mojo/public/tools/bindings/mojom.gni
@@ -745,6 +745,8 @@ template("mojom") {
       enabled_features += [ "is_mac" ]
     } else if (is_win) {
       enabled_features += [ "is_win" ]
+    } else if (is_qnx) {
+      enabled_features += [ "is_qnx" ]
     }
```

Both patches are registered in `cef/patch/patch.cfg` and the existing
`screen_ai_features_qnx_service.patch` is unchanged. Together the three
patches make `enable_screen_ai_service = true` viable on QNX without any
runtime sandbox wiring (the `sandbox_consumers_disable_linux_sandbox_qnx`
patch already guards the `screen_ai_sandbox_hook` Linux dep with `!is_qnx`,
so the runtime side stays unhooked).

## Verification

- `gn gen out/qnx_release` succeeds (32977 targets, 2867ms) after both patches
  are applied.
- `ninja -C out/qnx_release gen/sandbox/policy/mojom/sandbox.mojom-data-view.h`
  produces a `Sandbox` enum that now includes `kScreenAI` (mtime updates,
  grep finds `kScreenAI` in the generated header).
- `ninja -C out/qnx_release gen/services/screen_ai/public/mojom/screen_ai_factory.mojom.h`
  produces a header that now contains
  `static constexpr auto kServiceSandbox = sandbox::mojom::Sandbox::kScreenAI;`
  on both the `ScreenAIServiceShutdownHandler` and `ScreenAIServiceFactory`
  interface classes.
- The wider `cefsimple` build was last seen progressing past the screen_ai
  compile unit after the patches were applied; full end-to-end link
  verification on this host was not completed before the user requested a stop
  on this incident, so the next session should resume from
  `./out/qnx_release/ninja_qnx.sh cef` and watch for the next blocker.

## Files touched

- `cef/patch/patches/qnx/chromium/sandbox_mojom_kScreenAI_qnx.patch` (new)
- `cef/patch/patches/qnx/chromium/mojom_gni_is_qnx_enabled_features.patch` (new)
- `cef/patch/patch.cfg` (registered both new patches in apply order)
- `cef/patch/patches/qnx/chromium/screen_ai_features_qnx_service.patch`
  (regenerated SHA headers only; functional content unchanged)

## Related notes

- `docs/qnx/patch-hygiene.md` — patch format verification, regenerate-from-tree
- `docs/qnx/history/build-errors/compile/build-graph/qnx-blink-font-platform-data-dsf-fallback.md` —
  the preceding compile-stage failure (the one that surfaced screen_ai in the
  first place because the wider cefsimple target set was being built for the
  first time)
- `docs/qnx/build-error-index.md` — search hint:
  `rg -n "kServiceSandbox\|ScreenAIServiceFactory\|kScreenAI\|enabled_features\|conditional_features" docs/qnx/history/build-errors/compile`
