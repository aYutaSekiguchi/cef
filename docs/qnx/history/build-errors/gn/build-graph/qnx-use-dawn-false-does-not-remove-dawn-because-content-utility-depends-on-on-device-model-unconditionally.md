# QNX use_dawn=false does not remove Dawn because content/utility depends on on_device_model unconditionally

- Date: 2026-06-06
- Signature: `renderdoc_app.h:43:2: error: "Unknown platform"` (dawn/native/sources/RenderDoc.o)
- Stage: gn
- Category: build-graph
- Scope: content, chrome/browser/ui, services/on_device_model, third_party/dawn

## Symptoms

After a clean `gn gen` with `use_dawn = false` in `out/qnx_release/args.gn`, `ninja cefsimple` still compiled dawn native sources:

```
FAILED: obj/third_party/dawn/src/dawn/native/sources/RenderDoc.o
  renderdoc_app.h:43:2: error: "Unknown platform"
FAILED: obj/third_party/dawn/src/dawn/native/sources/TextureVk.o
FAILED: obj/third_party/dawn/src/dawn/native/sources/MemoryServiceImplementationOpaqueFD.o
FAILED: obj/third_party/dawn/src/dawn/native/sources/MemoryServiceImplementationDmaBuf.o
FAILED: obj/third_party/dawn/src/dawn/native/static/VulkanBackend.o
```

`gn path` revealed the dependency chain:

```
//cef:cefsimple → //cef:libcef → //cef:libcef_static
  → //content/public/utility:utility_sources
    → //content/utility:utility (unconditional deps on on_device_model)
      → //content/utility/on_device_model:on_device_model_sandbox_init
        → //third_party/dawn/src/dawn/native:static → sources
      → //services/on_device_model:on_device_model_service → dawn
```

A second path via chrome:

```
//cef:libcef_static → //chrome:dependencies → //chrome/browser:browser
  → //chrome/browser/ui:ui → //chrome/browser/ui/webui/on_device_internals
    → //services/on_device_model/ml:ml_no_internal → dawn
```

## Root cause

1. **`content/utility/BUILD.gn`**: Four on_device_model deps were listed unconditionally in the `deps` array.
2. **`chrome/browser/ui/BUILD.gn`**: Two on_device_model deps and `on_device_internals` public_dep were unconditional.
3. **`chrome/browser/ui/webui/on_device_internals/BUILD.gn`**: `ml_no_internal` dep was unconditional.
4. **`content/utility/on_device_model/BUILD.gn`**: The `!is_fuchsia` guard that pulls in dawn was always true for QNX (not Fuchsia).
5. **`use_on_device_model_service = false`** (a GN variable from `on_device_model.gni`) only controlled the service's internal behavior, but none of the `BUILD.gn` files checked it for their deps.
6. QNX is treated as `is_linux` by GN, so `use_on_device_model_service` defaults to true unless explicitly overridden.

## Fix pattern

Introduce a new GN variable `enable_on_device_model` that gates all on_device_model integration at the content/ and chrome/ levels. Default it to `use_on_device_model_service && use_dawn` so it automatically becomes false on QNX (where `use_dawn = false`). Move all on_device_model deps behind `if (enable_on_device_model)` guards.

## Applied change

1. **Created `content/utility/on_device_model/features.gni`** — defines `enable_on_device_model = use_on_device_model_service`. (Note: `use_dawn` is checked separately in BUILD.gn files, not here, because GN declare_args evaluation order does not guarantee `use_dawn` is available in all import contexts.)
2. **Modified `content/utility/on_device_model/BUILD.gn`** — switched import to features.gni; changed `!is_fuchsia` to `!is_fuchsia && enable_on_device_model`
3. **Modified `content/utility/BUILD.gn`** — moved 4 unconditional on_device_model deps into `if (enable_on_device_model)` block
4. **Modified `chrome/browser/ui/BUILD.gn`** — moved `on_device_model/public/cpp`, `on_device_model/public/mojom`, and `on_device_internals` public_dep into `if (enable_on_device_model)` blocks
5. **Modified `chrome/browser/ui/webui/on_device_internals/BUILD.gn`** — moved `ml_no_internal` dep into `if (enable_on_device_model)` block
6. **Created CEF patch**: `cef/patch/patches/qnx/chromium/enable_on_device_model_qnx.patch`
7. **Registered patch** in `cef/tools/cef_create_projects_qnx.sh` (`UNREGISTERED_CHROMIUM_PATCHES`) and `cef/patch/patch.cfg`
8. **Removed redundant `enable_on_device_model = false`** from `args.gn` — the default (`use_dawn = false`) now handles it automatically

## Verification

- `gn path //cef:cefsimple //third_party/dawn/src/dawn/native:sources` → "No non-data paths found"
- `ninja -C out/qnx_release -n cefsimple | grep "dawn/src/dawn/native"` → 0 matches
- Remaining dawn targets in build graph are all `clang_x64` (host toolchain tint), which are Linux x64 and do not hit QNX platform errors
- Targets reduced from 34792 → 34709 (83 targets removed)
- Prior 5 FAILED dawn targets (RenderDoc.o, TextureVk.o, MemoryServiceImplementationOpaqueFD.o, MemoryServiceImplementationDmaBuf.o, VulkanBackend.o) all eliminated from the build

## Files touched

- `content/utility/on_device_model/features.gni` (new)
- `content/utility/on_device_model/BUILD.gn`
- `content/utility/BUILD.gn`
- `chrome/browser/ui/BUILD.gn`
- `chrome/browser/ui/webui/on_device_internals/BUILD.gn`
- `cef/patch/patches/qnx/chromium/enable_on_device_model_qnx.patch` (new)
- `cef/patch/patch.cfg`
- `cef/tools/cef_create_projects_qnx.sh`
- `out/qnx_release/args.gn` (comment update; removed redundant `enable_on_device_model = false`)

## Related notes

- `docs/qnx/fixes-and-decisions.md` — Section #50 (WebGPU/Dawn deferred on QNX)
- Prior attempt to disable Dawn via `use_dawn = false` alone (insufficient — content/utility deps were unconditional)
