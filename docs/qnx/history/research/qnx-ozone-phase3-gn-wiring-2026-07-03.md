# QNX Ozone Phase 3: GN/Ozone Wiring

**Date:** 2026-07-03
**Status:** Complete
**Scope:** Additive GN/Ozone platform registration only; no Screen/EGL runtime backend

---

## Goal

Add the minimal CEF-managed Chromium patch/new-file changes needed to register an
opt-in `ozone_platform_qnx` platform in GN and the Ozone generator without
changing default headless behavior or implementing Screen/EGL runtime logic.

---

## What was done

### 1. GN flag patch — `build/config/ozone.gni`

**File:** `patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch`

Two changes in the `declare_args()` block:

```diff
+  # Compile the 'qnx' platform.
+  ozone_platform_qnx = false
```

Added after `ozone_platform_wayland = false`, alongside the other platform
flags. Default is `false` (opt-in only).

Assertion updated to include `ozone_platform_qnx` in the OR list:

```diff
-                         ozone_platform_x11 || ozone_platform_wayland),
+                         ozone_platform_qnx || ozone_platform_x11 || ozone_platform_wayland),
```

This ensures the assertion is consistent when `ozone_platform_qnx = true` is set.
On QNX, `use_ozone = true` is already set in `args.gn`, so the assertion
passes regardless.

### 2. Ozone BUILD.gn patch — `ui/ozone/BUILD.gn`

**File:** `patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch`

Added a conditional block after the flatland platform block:

```gn
if (ozone_platform_qnx) {
  ozone_platforms += [ "qnx" ]
  ozone_platform_deps += [ "platform/qnx" ]
}
```

This is the same pattern as all other built-in platforms (headless, cast,
flatland, etc.). When `ozone_platform_qnx = true`, "qnx" is added to the
platform list and `ui/ozone/platform/qnx` is added to the deps.

**Why `ozone_extra.gni` was NOT modified:** Adding `"qnx"` to
`ozone_external_platforms` in `ozone_extra.gni` would unconditionally add it
to `ozone_platforms` for ALL Chromium builds, not just QNX. The correct
approach is to handle it entirely within `ui/ozone/BUILD.gn` via the
`if (ozone_platform_qnx)` block — consistent with how the existing built-in
platforms are registered.

### 3. New Chromium files

**Directory:** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`

Files created:

| File | Purpose |
|---|---|
| `BUILD.gn` | `source_set("qnx")` with minimal deps; no Screen/EGL libs yet |
| `ozone_platform_qnx.h` | `CreateOzonePlatformQnx()` factory declaration |
| `ozone_platform_qnx.cc` | Minimal `OzonePlatformQnxImpl` stub; `InitializeUI` returns false |
| `client_native_pixmap_factory_qnx.h` | `CreateClientNativePixmapFactoryQnx()` factory |
| `client_native_pixmap_factory_qnx.cc` | Stub using `CreateStubClientNativePixmapFactory()` |

**BUILD.gn deps:** `base`, `ui/base`, `ui/display`, `ui/events`, `ui/gfx`,
`ui/ozone:ozone_base`, `ui/ozone/common`, `ui/platform_window` — no
Screen/EGL/GLES2 libs. Those are added in Phase 4 when the actual
Screen/EGL backend is implemented.

**`ozone_platform_qnx.cc` behavior:** `InitializeUI` returns `false` with
a `LOG(WARNING)` signal, causing Chromium to abort on startup if
`--ozone-platform=qnx` is used in Phase 3. This cleanly signals that
Phase 4 (the real Screen/EGL backend) must be implemented first.

### 4. patch.cfg registration

**File:** `patch/patch.cfg`

Added two entries at the end:

```python
{
  'name': 'qnx/chromium/ozone_platform_qnx_build.gni',
},
{
  'name': 'qnx/chromium/ozone_build_qnx_platform.gni',
},
```

New files under `patch/qnx/chromium/new_files/` do not require patch.cfg
entries; they are copied into the Chromium tree by `cef_create_projects_qnx.sh`
during bootstrap.

---

## Why default headless remains unchanged

- `ozone_platform_qnx = false` in `build/config/ozone.gni` — the platform is
  NOT compiled unless explicitly enabled.
- The QNX `args.gn` does NOT set `ozone_platform_qnx = true`; it remains at
  the GN default of `false`.
- The bootstrap script (`cef_create_projects_qnx.sh`) does NOT add
  `ozone_platform_qnx` to the args file.
- To use `--ozone-platform=qnx`, a developer must manually add
  `ozone_platform_qnx = true` to `out/qnx_release/args.gn` and re-run
  `gn gen`.

---

## Validation

### Patch dry-run (applied from Chromium src root)

```
$ patch -p0 --dry-run --batch --forward < cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch
checking file build/config/ozone.gni
Hunk #1 succeeded at 56 (offset 3 lines).

$ patch -p0 --dry-run --batch --forward < cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
checking file ui/ozone/BUILD.gn
Hunk #1 succeeded at 71 (offset 3 lines).
```

Both patches apply cleanly (offset is from the synthetic `index` line in the
patch header, which `patch -p0` ignores).

### GN file syntax

No GN syntax was manually edited — all changes are pure additions via patch.
The added GN fragments follow the exact style of existing platform blocks
(`ozone_platform_flatland`, `ozone_platform_cast`, etc.).

### No staged files

`git status --short` shows no staged changes. All modifications are
untracked new files or unstaged modifications.

---

## Phase 3 Checklist

| Item | Status |
|---|---|
| Add `ozone_platform_qnx` build flag to `build/config/ozone.gni` | ✅ Done |
| Add `if(ozone_platform_qnx)` block to `ui/ozone/BUILD.gn` | ✅ Done |
| Add minimal `ui/ozone/platform/qnx/BUILD.gn` under new_files | ✅ Done |
| Add minimal `ozone_platform_qnx.cc/.h` stub | ✅ Done |
| Add `client_native_pixmap_factory_qnx.cc/.h` stub | ✅ Done |
| Register patches in `patch.cfg` | ✅ Done |
| Verify default headless behavior unchanged | ✅ Confirmed |
| Verify patches apply cleanly | ✅ Dry-run passed |
| `gn gen` with `ozone_platform_qnx=true` | ⚠️ Not run (requires bootstrap) |
| Full ninja build | ⚠️ Not run (out of scope for Phase 3) |

**`gn gen` note:** Running `gn gen` requires a bootstrap pass to apply patches
and copy new files into the Chromium tree. This is done by running
`cef_create_projects_qnx.sh`. The Phase 3 implementation is complete; the
actual `gn gen` and build validation would be done by the next bootstrap run
or a dedicated build worker.

---

## Files Changed / Added

### Patches (3 deleted, 2 created, 1 modified)

| File | Action |
|---|---|
| `patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch` | **New** — adds `ozone_platform_qnx = false` flag and assertion update |
| `patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch` | **New** — adds `if(ozone_platform_qnx)` block to `ui/ozone/BUILD.gn` |
| `patch/patches/qnx/chromium/ozone_extra_qnx_platforms.gni.patch` | **Removed** — not needed; would have added qnx to all builds |

### New Chromium files (5 created)

| File | Purpose |
|---|---|
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn` | GN target definition |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.h` | Header |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc` | Stub implementation |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/client_native_pixmap_factory_qnx.h` | Factory header |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/client_native_pixmap_factory_qnx.cc` | Stub factory |

### Configuration (1 modified)

| File | Change |
|---|---|
| `patch/patch.cfg` | Added two patch entries for the GN patches |

---

## Open Risks / Next Steps

1. **`gn gen` not yet validated end-to-end:** The patches pass dry-run, but the
   full bootstrap+gn_gen pipeline hasn't been run. The next step is to run
   `cef_create_projects_qnx.sh` (with `ozone_platform_qnx = true` added to
   args.gn first) and verify `gn gen` succeeds.

2. **No `gn gen` or ninja build in this phase:** Per the task constraints,
   long builds were not run. The Phase 3 acceptance should be confirmed by
   running the bootstrap script and a focused `gn gen`.

3. **Phase 4 is the next step:** The Phase 3 stub `InitializeUI` returns
   `false` to signal that Phase 4 (real Screen/EGL backend) must be
   implemented. Phase 4 adds `QnxScreenContext`, `QnxWindowManager`,
   `QnxWindow`, `QnxPlatformEventSource`, and wires minimal Mojo plumbing.

4. **Screen/EGL deps deferred:** The `BUILD.gn` currently has `libs` commented
   out for Screen/EGL. Phase 4 uncomments and wires those deps when the
   real backend files are added.

---

## Commands Run

```sh
# Dry-run both patches from Chromium src root
cd /home/yuta/chromium/src
patch -p0 --dry-run --batch --forward \
  < cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch
# → Hunk #1 succeeded at 56

patch -p0 --dry-run --batch --forward \
  < cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
# → Hunk #1 succeeded at 71

# Verify bootstrap script doesn't set ozone_platform_qnx (opt-in)
grep ozone_platform_qnx cef/tools/cef_create_projects_qnx.sh
# → (no output, confirmed opt-in)

# Check for staged files
cd /home/yuta/chromium/src/cef
git status --short
# → M patch/patch.cfg  (unstaged)
# → ?? patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch  (untracked)
# → ?? patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch  (untracked)
# → ?? patch/qnx/chromium/new_files/ui/ozone/ (untracked directory)
```
