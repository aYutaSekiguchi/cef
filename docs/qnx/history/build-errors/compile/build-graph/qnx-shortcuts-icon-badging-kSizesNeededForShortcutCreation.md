# QNX: `kSizesNeededForShortcutCreation` undeclared in `icon_badging.cc`

- Date: 2026-06-15
- Signature: `error: use of undeclared identifier 'kSizesNeededForShortcutCreation'`, `icon_badging.cc:290`
- Stage: compile
- Category: build-graph
- Scope: `chrome/browser/shortcuts/icon_badging.cc` and `icon_badging_unittest.cc` (single one-line `#elif` guard)

## Symptoms

- `out/qnx_release/ninja_qnx.sh cef` aborts at step 28663/61187 with a single failure:
  `FAILED: obj/chrome/browser/shortcuts/shortcuts/icon_badging.o`
- The clang error reads:
  ```
  ../../chrome/browser/shortcuts/icon_badging.cc:290:41: error: use of undeclared identifier 'kSizesNeededForShortcutCreation'
    290 |   for (const ShortcutSize needed_size : kSizesNeededForShortcutCreation) {
        |                                         ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  1 error generated.
  ```
- This is the second compile-stage failure encountered when extending the QNX
  build from the validated `base_unittests` baseline to the wider `cefsimple`
  target set. The first two failures (`font_platform_data`, `screen_ai
  kServiceSandbox`) are documented separately; this note covers the
  `icon_badging` family of failures.

## Root cause

`kSizesNeededForShortcutCreation` is declared in
`chrome/browser/shortcuts/icon_badging.cc:58-68` with a three-way platform
switch:

```cpp
#if BUILDFLAG(IS_MAC)
constexpr ShortcutSize kSizesNeededForShortcutCreation[] = { ... };
#elif BUILDFLAG(IS_LINUX)
constexpr ShortcutSize kSizesNeededForShortcutCreation[] = {ShortcutSize::k32,
                                                            ShortcutSize::k128};
#elif BUILDFLAG(IS_WIN)
constexpr ShortcutSize kSizesNeededForShortcutCreation[] = { ... };
#endif
```

There is no `#elif BUILDFLAG(IS_QNX)` branch, so on QNX the array is undeclared
when `icon_badging.cc:290` iterates over it. The same pattern exists in
`icon_badging_unittest.cc:110-117` (the `GetOsSpecificSizes()` test helper
which mirrors the array length), so the fix has to land in both files.

## Why follow the `variations_service_qnx` / `user_agent_utils_qnx` pattern

The existing QNX port handles the same shape in many other call sites by
extending an existing `#elif` branch with `|| BUILDFLAG(IS_QNX)` instead of
adding a separate QNX branch. Examples:

- `cef/patch/patches/qnx/chromium/variations_service_qnx.patch`:
  `IS_LINUX || IS_BSD || IS_SOLARIS` → `IS_LINUX || IS_BSD || IS_SOLARIS || IS_QNX`
  with a single shared "linux" return value.
- `cef/patch/patches/qnx/chromium/user_agent_utils_qnx.patch`:
  `IS_LINUX || IS_CHROMEOS` → `IS_LINUX || IS_CHROMEOS || IS_QNX` with the
  same `kUnifiedPlatformLinuxX64` constant.
- `cef/patch/patches/qnx/chromium/chrome_browser_main_extra_parts_enterprise_qnx.patch`:
  `(IS_LINUX || IS_MAC || IS_WIN) && COND` → `(IS_LINUX || IS_MAC || IS_WIN || IS_QNX) && COND`.

QNX is a POSIX/Unix-like desktop OS without a distinct shortcut-size convention,
so the Linux branch's `k32 + k128` pair is the right default for it; merging
QNX into the Linux branch is the same shape of fix the rest of the QNX port
already uses for analogous "no QNX branch" failures.

## Fix pattern

Add `|| BUILDFLAG(IS_QNX)` to the existing `#elif BUILDFLAG(IS_LINUX)` guard
in both `icon_badging.cc` and `icon_badging_unittest.cc`. The Linux branch's
two-element `{k32, k128}` array (and the `return 2` length in the unit test
helper) becomes the shared fallback for both Linux and QNX. No new branch,
no new array, no behavior change on Linux.

## Applied change

`cef/patch/patches/qnx/chromium/shortcuts_icon_badging_qnx_is_linux_fallback.patch`
(registered in `cef/patch/patch.cfg`):

```diff
--- a/chrome/browser/shortcuts/icon_badging.cc
+++ b/chrome/browser/shortcuts/icon_badging.cc
@@ -58,7 +58,7 @@ enum class BadgeSize {
 constexpr ShortcutSize kSizesNeededForShortcutCreation[] = {
     ShortcutSize::k16, ShortcutSize::k32, ShortcutSize::k128,
     ShortcutSize::k256, ShortcutSize::k512};
-#elif BUILDFLAG(IS_LINUX)
+#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)
 constexpr ShortcutSize kSizesNeededForShortcutCreation[] = {ShortcutSize::k32,
                                                             ShortcutSize::k128};
 #elif BUILDFLAG(IS_WIN)

--- a/chrome/browser/shortcuts/icon_badging_unittest.cc
+++ b/chrome/browser/shortcuts/icon_badging_unittest.cc
@@ -110,7 +110,7 @@ void WriteTestIconsToDiskOrDie(const gfx::ImageFamily& family) {
 int GetOsSpecificSizes() {
 #if BUILDFLAG(IS_MAC)
   return 5;
-#elif BUILDFLAG(IS_LINUX)
+#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)
   return 2;
 #elif BUILDFLAG(IS_WIN)
   return 4;
```

Regenerated via `git diff --no-prefix --relative --full-index` from a working
tree where both files were edited. `patch -p0 --batch --dry-run` on a clean
source tree exits 0 with `checking file chrome/browser/shortcuts/icon_badging.cc`
and `checking file chrome/browser/shortcuts/icon_badging_unittest.cc`.

## Verification

- `patch -p0 --batch --dry-run` on a clean source tree exits 0 for both files.
- A single-file compile (`ninja -C out/qnx_release
  obj/chrome/browser/shortcuts/shortcuts/icon_badging.o`) succeeds after the
  patch is applied, producing a 56 KB `.o` artifact.
- The full `cefsimple` build progressed past step 28663 (the previous failure
  point) and reached step 1947/36755 in the second clean rebuild before
  exposing a different failure in `sharing_hub_bubble_controller_desktop_impl.h`
  (an unrelated QNX-side override-keyword issue, recorded separately).
- No behavior change on Linux (the existing branch is preserved verbatim).
- On QNX the array and the unit-test length are now the same as Linux, which
  is the existing QNX-port convention for the analogous `IS_LINUX || IS_QNX`
  fallbacks cited above.

## Files touched

- `cef/patch/patches/qnx/chromium/shortcuts_icon_badging_qnx_is_linux_fallback.patch` (new)
- `cef/patch/patch.cfg` (registered the new patch in apply order)

## Related notes

- `docs/qnx/history/build-errors/compile/build-graph/qnx-blink-font-platform-data-dsf-fallback.md` —
  the previous compile-stage failure uncovered by extending to `cefsimple`
- `docs/qnx/history/build-errors/compile/build-graph/qnx-mojom-is-qnx-enabled-features.md` —
  the screen_ai `kServiceSandbox` failure that surfaced between the font and
  icon_badging fixes
- `docs/qnx/patch-hygiene.md` — the patch format / regenerate-from-tree
  workflow used here
