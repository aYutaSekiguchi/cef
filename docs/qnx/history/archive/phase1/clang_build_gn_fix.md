# BUILD.gn QNX Support Fix

## Investigation Results

### 1. clang_lib("compiler_builtins") Target Structure

Inside `clang_lib` template at `build/config/clang/BUILD.gn:160-231`:

```python
} else if (is_android) {
  _dir = "linux"
  # _suffix settings...
} else {
  assert(false)  # Unhandled target platform  ← Error here (line 231)
}
```

**Problem**: `is_qnx` matches no condition, causing the assertion to fail.

### 2. Existing Platform Directory Mappings

| Platform | `_dir` value | Purpose |
|----------|-------------|---------|
| Windows | `windows` | libclang_rt.*-x86_64.lib |
| macOS/iOS | `darwin` | libclang_rt.*_osx.a |
| Linux/ChromeOS | `*-unknown-linux-gnu` | libclang_rt.*-x86_64-linux-gnu.a |
| Fuchsia | `*-unknown-fuchsia` | libclang_rt.*-x86_64-unknown-fuchsia.a |
| Android | `linux` + suffix | libclang_rt.*-aarch64-android.a |

### 3. Directory Value for QNX

**Proposed: `_dir = "nto"`**

Rationale:
- Standard LLVM Clang/QNX target triple prefix
- Matches naming convention in `build/config/rust.gni:285` rust_abi_target:
  - `aarch64-unknown-nto-qnx800`
  - `x86_64-pc-nto-qnx800`
- Places `lib/clang/*/lib/nto/` in LLVM lib directory

### 4. check-ipc Plugin (line 97)

```python
if (is_linux || is_chromeos || is_android || is_fuchsia) {
  plugin_arguments += [ "check-ipc" ]
}
```

**Recommendation: Do not include**

Rationale:
- check-ipc plugin is Linux/Android static analysis only
- QNX links with qcc (GCC), not needed
- QNX has very different libc/ABI from other Linux-like platforms

---

## Minimal Fix Diff

```diff
--- a/build/config/clang/BUILD.gn
+++ b/build/config/clang/BUILD.gn
@@ -227,6 +227,8 @@ template("clang_lib") {
           assert(false)  # Unhandled cpu type
         }
       } else if (is_qnx) {
+        _dir = "nto"
+      } else {
         assert(false)  # Unhandled target platform
       }
```

---

## check-ipc Plugin (line 97)

No change needed. QNX should be explicitly excluded (Linux/Android only).

---

## Production Considerations

On QNX environment:
1. `lib/clang/<version>/lib/nto/` needs `libclang_rt.builtins.a`
2. qcc's libgcc provides equivalent functionality, so linker falls back
3. Can work around with empty dummy file if no actual harm
4. Full support: copy clang_rt.builtins.a from qcc sysroot
