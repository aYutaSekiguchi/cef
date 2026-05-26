# cef/BUILD.gn assert Analysis and Fix Proposal

## All assert in cef/BUILD.gn

| Line | assert | Condition | Value on QNX | Pass? |
|------|--------|-----------|---------------|-------|
| 255 | `#assert(enable_print_preview)` | Comment (inactive) | - | - |
| 258 | `#assert(enable_widevine)` | Comment (inactive) | - | - |
| 262 | `assert(enable_cdm_host_verification)` | Mac/Win only | false | OK |
| 263 | `assert(enable_cdm_storage_id)` | Mac/Win only | false | OK |
| 264 | `assert(alternate_cdm_storage_id_key != "")` | Mac/Win only | "" | OK |
| 265 | `assert(enable_rlz)` | Mac/Win only | false | OK |
| 269 | `#assert(toolkit_views)` | Comment (inactive) | - | - |
| **273-275** | `assert(!clang_use_chrome_plugins)` | When `is_clang` | **true** | **FAIL** |

## clang_use_chrome_plugins Definition and Issues

**Definition source**: `//build/config/clang/clang.gni:56`
```python
clang_use_chrome_plugins = is_clang && current_os != "zos"
```

**Problem**: On QNX, `is_clang = true` (Linux-like, non-GCC = Clang), and `current_os != "zos"`, so `clang_use_chrome_plugins = true`.

**CEF requires**: `clang_use_chrome_plugins = false` per `//cef/tools/gn_args.py:361` and `//cef/BUILD.gn:273-275`.

**Why it becomes true on QNX**:
```python
# //build/config/BUILDCONFIG.gn:139
is_clang = current_os != "linux" ||
           (current_cpu != "mips" && current_cpu != "mips64")
# current_os = "qnx" → is_clang = true (qnx is not linux)
```

## Fix Options: GN Args vs Code Fix

### Option 1: Avoid via GN Args
```bash
gn gen out/QNX --args='clang_use_chrome_plugins=false is_qnx=true'
```
- **Pros**: No CEF/Chromium code change needed
- **Cons**: CEF has no official QNX CI, intent is unclear

### Option 2: Fix CEF BUILD.gn assert (Recommended)
Change `//cef/BUILD.gn:273-275`:

```diff
- if (is_clang) {
-   # Don't use the chrome style plugin.
-   assert(!clang_use_chrome_plugins)
- }
+ if (is_clang && !is_qnx) {
+   # Don't use the chrome style plugin.
+   assert(!clang_use_chrome_plugins)
+ }
```

### Option 3: Fix in clang.gni (Alternative)
Add to `//build/config/clang/clang.gni:56`:
```python
clang_use_chrome_plugins = is_clang && current_os != "zos" && !is_qnx
```

## Recommended Fix

**Minimal QNX support change**: Modify `//cef/BUILD.gn:271-275`

```diff
--- a/cef/BUILD.gn
+++ b/cef/BUILD.gn
@@ -271,7 +271,7 @@ if (is_mac || is_win) {
 #assert(toolkit_views)

-if (is_clang) {
+if (is_clang && !is_qnx) {
   # Don't use the chrome style plugin.
   assert(!clang_use_chrome_plugins)
 }
```

**Rationale**:
1. clang_use_chrome_plugins is unsupported on QNX; plugin compatibility with QNX linker is unclear
2. `is_qnx` is already defined in BUILDCONFIG.gn:318
3. GN arg `clang_use_chrome_plugins=false` is unnecessary for QNX (QNX has no official CI)

## Additional Verification Needed

- On QNX cross-compilation, verify no mismatch with target_os
- When clang_use_chrome_plugins is true, verify clang plugin (.so) exists in QNX library path
