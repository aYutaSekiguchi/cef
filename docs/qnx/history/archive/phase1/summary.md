# QNX Phase 1 - GN Gen Success Summary

## Overview
Records all changes and errors up to the point where `gn gen --args='target_os="qnx" target_cpu="x64" ...'` succeeds.

---

## Changed Files (git diff --stat)

```
build/config/BUILDCONFIG.gn               |  6 ++++
build/config/clang/BUILD.gn               |  8 +++++++
build/config/rust.gni                      |  8 +++++++
build/rust/known-target-triples.txt       |  2 ++
components/supervised_user/buildflags.gni |  2 +-
content/test/BUILD.gn                      |  2 +-
6 files changed, 25 insertions(+), 3 deletions(-)
```

---

## Each File's Changes

### 1. build/config/BUILDCONFIG.gn

```diff
@@ -276,6 +276,9 @@ if (target_os == "android") {
+} else if (target_os == "qnx") {
+  assert(host_os == "linux", "QNX builds are only supported on Linux.")
+  _default_toolchain = "//build/toolchain/qnx:clang_$target_cpu"
   } else if (target_os == "emscripten") {

@@ -312,10 +315,11 @@ if (custom_toolchain != "") {
+is_qnx = current_os == "qnx"
   is_chromeos = current_os == "chromeos"
   is_fuchsia = current_os == "fuchsia"
   is_ios = current_os == "ios"
-  is_linux = current_os == "linux"
+  is_linux = current_os == "linux" || is_qnx
   is_mac = current_os == "mac"
+5 -1
```

**What changed:**
- Added `target_os == "qnx"` case with host Linux constraint and toolchain specification
- Added `is_qnx` variable (`current_os == "qnx"`)
- Included `is_qnx` in `is_linux` (QNX is POSIX-compatible and similar to Linux)

---

### 2. build/config/clang/BUILD.gn

```diff
@@ -212,6 +212,14 @@ template("clang_lib") {
+      } else if (is_qnx) {
+        if (current_cpu == "x64") {
+          _dir = "x86_64-unknown-nto"
+        } else if (current_cpu == "arm64") {
+          _dir = "aarch64-unknown-nto"
+        } else {
+          assert(false)  # Unhandled cpu type
+        }
         } else if (is_android) {
           _dir = "linux"
+8 -0
```

**What changed:**
- Added `is_qnx` branch to `clang_lib` template
- Set QNX LLVM lib directory based on target triple

---

### 3. build/config/rust.gni

```diff
@@ -283,6 +283,14 @@ if (is_linux || is_chromeos) {
+} else if (is_qnx) {
+  if (current_cpu == "arm64") {
+    rust_abi_target = "aarch64-unknown-nto-qnx800"
+  } else if (current_cpu == "x64") {
+    rust_abi_target = "x86_64-pc-nto-qnx800"
+  } else {
+    assert(false, "Architecture not supported")
+  }
   } else if (is_ios) {
     if (current_cpu == "arm64e") {
       assert(target_platform == "iphoneos",
+8 -0
```

**What changed:**
- Set Rust ABI target triple for QNX

---

### 4. build/rust/known-target-triples.txt

```diff
@@ -41,5 +41,7 @@ x86_64-linux-android
+  x86_64-pc-nto-qnx800
+  aarch64-unknown-nto-qnx800
   powerpc64le-unknown-linux-gnu
   s390x-unknown-linux-gnu
+2 -0
```

---

### 5. components/supervised_user/buildflags.gni

```diff
@@ -6,5 +6,5 @@ declare_args() {
-      is_android || is_chromeos || is_ios || is_linux || is_mac || is_win
+      is_android || is_chromeos || is_ios || is_linux || is_mac || is_win || is_qnx
   }
+1 -1
```

---

### 6. content/test/BUILD.gn

```diff
@@ -3226,7 +3226,7 @@ test("content_unittests") {
-  if (is_android || is_linux || is_chromeos || is_mac || is_win || is_fuchsia) {
+  if (is_android || is_linux || is_chromeos || is_mac || is_win || is_fuchsia || is_qnx) {
+1 -1
```

---

## Error and Fix History (Chronological)

### Error 1: QNX Toolchain Undefined

**Error message:**
```
ERROR at //BUILD.gn:1: Undefined variable "//build/toolchain/qnx:clang_x64"
```

**Cause:** No toolchain definition exists for `target_os == "qnx"`

**Fix:** Created `build/toolchain/qnx/BUILD.gn` new

```gn
template("qnx_clang_toolchain") {
  gcc_toolchain(target_name) {
    cc = "${clang_base_path}/bin/clang"
    cxx = "${clang_base_path}/bin/clang++"
    ld = "${_qnx_host}/usr/bin/qcc"
    # ...
    toolchain_args = {
      current_os = "qnx"
      is_qnx = true
      is_clang = true
    }
  }
}
```

---

### Error 2: clang_lib Unhandled Platform

**Error message:**
```
ERROR at //build/config/clang/BUILD.gn:231:1: Assertion failed: assert(false)  # Unhandled target platform
```

**Cause:** `clang_lib` template doesn't handle `is_qnx`

**Fix:** Added `else if (is_qnx)` block to `build/config/clang/BUILD.gn`

---

### Error 3: ui/menus/BUILD.gn OS Assertion

**Error message:**
```
ERROR at //ui/menus/BUILD.gn:6:1: Assertion failed.
assert(is_win || is_mac || is_linux || is_chromeos || is_android || is_fuchsia || is_qnx)
```

**Cause:** `is_qnx` not included in platform assert

**Fix:** Added `is_qnx = current_os == "qnx"` to BUILDCONFIG.gn

---

### Error 4: components/crash/core/common/BUILD.gn Assertion

**Error message:**
```
ERROR at //components/crash/core/common/BUILD.gn:35:1: Assertion failed.
assert(use_crash_key_stubs || use_crashpad_annotation || is_castos)
```

**Cause:** QNX doesn't have crashpad settings

**Fix:** Set `use_crash_key_stubs=true` in GN args

---

### Error 5: clang_use_chrome_plugins and CEF

**Error message:**
```
ERROR at //cef/BUILD.gn:273-275: Assertion failed: assert(!clang_use_chrome_plugins)
```

**Cause:** `is_clang=true` on QNX → `clang_use_chrome_plugins=true` → CEF errors

**Fix:** Set `clang_use_chrome_plugins=false` in GN args

---

### Error 6: content/test/BUILD.gn Undefined data Variable

**Error message:**
```
ERROR at //content/test/BUILD.gn:3456:1: Undefined identifier 'data'
```

**Cause:** `is_qnx` missing from condition → `data` variable remains undefined when `data +=` executes

**Fix:** Added `|| is_qnx` to the condition

---

## Required GN Args

| Arg | Value | Reason |
|-----|-------|--------|
| `target_os` | `"qnx"` | Build target OS |
| `target_cpu` | `"x64"` or `"arm64"` | Target CPU |
| `clang_use_chrome_plugins` | `false` | CEF build assert avoidance |
| `use_crash_key_stubs` | `true` | crash component assert avoidance |
| `use_autogenerated_modules` | `false` | QNX libclang compatibility |
| `use_clang_modules` | `false` | QNX libclang compatibility |

---

## Successful gn gen Command

```bash
export QNX_HOST=<QNX_SDP_ROOT>/host/linux/x86_64
export QNX_TARGET=<QNX_SDP_ROOT>/target/qnx

gn gen out/qnx_x64 --args='
  target_os="qnx"
  target_cpu="x64"
  clang_use_chrome_plugins=false
  use_crash_key_stubs=true
  use_autogenerated_modules=false
  use_clang_modules=false
'
```

**Result:** ✅ SUCCESS

---

## Generated args.gn

```gn
target_os = "qnx"
target_cpu = "x64"
clang_use_chrome_plugins = false
use_crash_key_stubs = true
use_autogenerated_modules = false
use_clang_modules = false
```

---

## Issues for Phase 2

1. `ui/menus/BUILD.gn` fixed, but other places may need `is_qnx`
2. Need to verify `libclang_rt.builtins.a` exists in QNX sysroot
3. Need to verify qcc linker compatibility
4. Remaining issues before proceeding to actual compilation test (ninja)

---

*Generated: 2025-05-08*
