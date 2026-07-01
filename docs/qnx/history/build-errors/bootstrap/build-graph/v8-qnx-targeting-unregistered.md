# v8-qnx-targeting-unregistered

- Stage: bootstrap / build-graph
- Category: build-graph
- First observed: 2026-07-01
- Affected target: `ceftests` (and any v8-consuming target) on a
  clean-tree-to-bootstrap run

## Signature

After a fresh `git checkout -f && gclient sync -f -R &&
cef_create_projects_qnx.sh` recipe on the production-ready fontconfig
tree, `ninja ceftests` dies at step ~1830 with:

```
[1815/56239] CXX obj/v8/v8_libbase/platform-posix.o
FAILED: obj/v8/v8_libbase/platform-posix.o
../../v8/src/base/platform/platform-posix.cc:78:10: fatal error:
    'sys/syscall.h' file not found
   78 | #include <sys/syscall.h>
      |          ^~~~~~~~~~~~~~~
```

`v8/src/base/platform/platform-posix.cc` is compiled unconditionally for
every POSIX target. QNX SDP 8 ships no `sys/syscall.h`, so the QNX build
fails to produce `platform-posix.o`, which cascades into a missing
`libv8_libbase.a` member and a `ceftests` link failure for `v8::base::*`.

The DejaVu workaround era never hit this signature because the
build was incremental: `platform-posix.o` and `libv8_libbase.a` were
already on disk from a previous state of the QNX SDP / source tree,
and ninja skipped them on the next invocation.

## Root cause

`cef/patch/patches/qnx/chromium/v8_qnx_targeting.patch` is the CEF
patch that teaches v8 about QNX. It does three things on top of
upstream v8:

1. Adds `V8_TARGET_OS_QNX` to the `enabled_external_v8_defines` list
   when `target_os == "qnx"`, so v8 knows QNX is a recognized
   target.
2. Adds `!V8_OS_QNX` to the `#if !defined(_AIX) && !defined(V8_OS_FUCHSIA)
   && !V8_OS_ZOS` guard around `<sys/syscall.h>` in
   `v8/src/base/platform/platform-posix.cc`. Without this guard the
   QNX toolchain cannot compile `platform-posix.cc`.
3. Adds a QNX branch to `GetCurrentStackPosition()` in
   `platform-posix.cc` (QNX's `pthread_t` is an `int`, not a pointer,
   so the existing `reinterpret_cast` is undefined behavior).

The patch file exists at `patch/patches/qnx/chromium/v8_qnx_targeting.patch`
and is well-formed. **But `cef/patch/patch.cfg` does not register it.**
Bootstrap only applies patches named in `patch.cfg`; orphaned patches
in `patches/qnx/chromium/` are skipped silently.

The patch was added to the QNX port long ago (predates the CEF-managed
patch registration discipline) and apparently landed in
`patches/qnx/chromium/` without a matching `patch.cfg` entry. Other v8
patches in the same directory (`v8_base64_atomic.patch`,
`v8_perfetto_trace_qnx.patch`, `v8_stack_limit_qnx.patch`,
`v8_background_compile_stack_size_qnx.patch`,
`v8_bytecode_expectations_qnx.patch`,
`v8_unittests_status_logall_qnx.patch`,
`v8_workloads_basic_functionality_stack_qnx.patch`) had the same issue;
all eight are now registered together with this fix.

## Fix

Register every `qnx/chromium/v8_*.patch` patch in `patch.cfg` with a
short comment explaining the QNX-specific issue each one addresses.
The registration goes in the alphabetical `v` section, immediately
before `qnx/chromium/variations_service_qnx`:

```python
{
  # QNX: register `qnx` as a V8 target_os so V8 picks the QNX
  # platform branch (pthread_self returns int, not a pointer; skip
  # <sys/syscall.h> in platform-posix.cc because QNX SDP 8 does not
  # ship it; use a QNX-specific stack-trace source). Without this,
  # the v8 link step fails with undefined references and a clean
  # bootstrap cannot reach `ninja ceftests`.
  'name': 'qnx/chromium/v8_qnx_targeting',
},
{
  # QNX: builtins-typed-array.cc calls base::Atomic64::Release_Store
  # on a uint64_t, which requires base64 atomic operations only
  # implemented for win/mac/linux/chromeos/android/ios/fuchsia in
  # base::subtle. QNX falls into the generic (non-atomic) branch and
  # fails to link. Add base::subtle::Release_Store/Acquire_Load for
  # 8-byte values on QNX so v8/src/builtins/typed-array code links.
  'name': 'qnx/chromium/v8_base64_atomic',
},
# ... and so on for the other six v8_qnx patches.
```

After registration, `cef_create_projects_qnx.sh` applies the patches
on a fresh tree; `git diff` against the patched tree shows the
`!V8_OS_QNX` guard around `<sys/syscall.h>` is present.

## Verification

After registration, the clean-tree-to-bootstrap recipe reaches the
end of `ninja ceftests`:

```
[53735/53736] SOLINK ./libcef.so
[53736/53736] LINK ./ceftests
```

`DownloadTest.*` passes 31/31 from the fresh build:

```
[==========] 31 tests from 1 test suite ran.
[  PASSED  ] 31 tests.
CT_QNX_PROD_V3_EC:0
```

`grep "!V8_OS_QNX" out/qnx_release/v8/src/base/platform/platform-posix.cc`
shows the guard is in place after bootstrap, and
`out/qnx_release/obj/v8/v8_libbase/platform-posix.o` is built (it was
missing in the previous state).

## Cross-references

- `cef/.agents/skills/qnx-cef-build/SKILL.md` describes the clean-tree
  recipe this note assumes.
- `docs/qnx/build-error-index.md` adds the search terms
  `v8_qnx_targeting_unregistered|v8_qnx_targeting_registered|v8
  platform-posix.cc sys/syscall.h|v8 BUILD.gn V8_TARGET_OS_QNX` to
  surface this category of issue.
- `docs/qnx/history/build-errors/bootstrap/build-graph/cef-managed-patch-registration-and-clean-bootstrap.md`
  is the broader pattern: every patch under `patches/qnx/chromium/`
  must be registered in `patch.cfg`, and the clean-tree recipe is the
  only reliable verification.