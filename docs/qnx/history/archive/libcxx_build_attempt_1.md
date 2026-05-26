# libc++ QNX Build Attempt 1 - Report

## Status: FAILED

## Error Summary
Build fails with:
```
fatal error: 'asm/errno.h' file not found
  1 | #include <asm/errno.h>
    |          ^~~~~~~~~~~~~
```

## Root Cause
The **sysroot is empty** (`--sysroot=`). Even though `--target=x86_64-unknown-nto` is correct, without a sysroot the compiler falls back to searching the **host system's include paths** (`/usr/include`), which contain Linux kernel headers.

The compilation command has:
```
--target=x86_64-unknown-nto
--sysroot=              <-- EMPTY! Should be <QNX_SDP_ROOT>/target/qnx
```

## Key Files

### Toolchain Definition
- `build/toolchain/qnx/BUILD.gn` (lines 24-26) - Sets `_target_flags = "--target=${_target} -D__QNXNTO__ ... --sysroot=${_qnx_target}"`
- `build/config/sysroot.gni` (lines 24-26) - Sets `sysroot = getenv("QNX_TARGET")` for QNX builds

### libc++ Build Definition  
- `buildtools/third_party/libc++/BUILD.gn` (lines 350-450) - libc++ target definition
- `buildtools/third_party/libc++abi/BUILD.gn` - libc++abi target definition

### libc++abi Ninja File (with correct target but wrong sysroot)
- `out/qnx_x64/obj/buildtools/third_party/libc++abi/libc++abi.ninja` - Contains:
  - `cflags = ... --target=x86_64-unknown-linux-gnu ... --sysroot=`

### Current Build Config
- `out/qnx_x64/args.gn` - Missing sysroot configuration

## Fix Required

Need to set `QNX_TARGET` environment variable **before** running `gn gen`:

```bash
export QNX_HOST=<QNX_SDP_ROOT>/host/linux/x86_64
export QNX_TARGET=<QNX_SDP_ROOT>/target/qnx
cd <CHROMIUM_SRC_ROOT>

# Remove existing build directory and regenerate
rm -rf out/qnx_x64
gn gen out/qnx_x64 --args='target_os="qnx" target_cpu="x64"'

# Build
ninja -C out/qnx_x64 libc++abi libc++
```

**Important**: `gn gen` must be run with `QNX_TARGET` set in the environment. The sysroot path is evaluated at `gn gen` time, not `ninja` time.

## Next Steps
1. Set `QNX_TARGET` and `QNX_HOST` environment variables
2. Re-run `gn gen` to regenerate build files with correct sysroot
3. Re-run `ninja -C out/qnx_x64 libc++abi libc++`