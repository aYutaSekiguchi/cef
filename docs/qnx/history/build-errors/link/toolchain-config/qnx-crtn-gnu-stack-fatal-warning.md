# QNX crtn.o executable-stack warning under fatal linker warnings

- Date: 2026-06-09
- Signature: `crtn.o: missing .note.GNU-stack section implies executable stack`
- Stage: link
- Category: toolchain-config
- Scope: `build/config/compiler/BUILD.gn`, QNX shared-library links

## Symptoms

During `base_unittests` on the `is_linux=false` experiment branch, the first shared-library links failed even though the diagnostic was only a linker warning:

```text
FAILED: libimmediate_crash_test_helper.so libimmediate_crash_test_helper.so.TOC
x86_64-pc-nto-qnx8.0.0-ld: warning: /home/yuta/qnx800/target/qnx/x86_64/lib/crtn.o: missing .note.GNU-stack section implies executable stack
```

`libtest_shared_library.so` failed with the same warning.

## Root cause

Chromium enables `-Wl,--fatal-warnings` in `config("linker")`. QNX SDP 8's startup object `crtn.o` does not carry `.note.GNU-stack`, so GNU ld warns that it implies an executable stack. With fatal linker warnings enabled, that QNX toolchain compatibility warning becomes a link failure.

## Fix pattern

Keep Chromium's fatal linker warnings enabled on QNX, but suppress only the executable-stack compatibility warning from the QNX-provided startup objects:

```gn
if (is_qnx) {
  ldflags += [ "-Wl,--no-warn-execstack" ]
}
```

Do not disable `--fatal-warnings` globally unless a future QNX linker issue cannot be narrowed.

## Applied change

Added `qnx/chromium/qnx_linker_no_warn_execstack`, registered immediately after `qnx/chromium/build_qnx_toolchain`.

## Verification

- Manual repro of the failing `qcc -shared` link with `-Wl,--no-warn-execstack` succeeded with exit 0 and no output.
- `cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root /home/yuta/qnx800` succeeded after the patch.
- `./out/qnx_release/ninja_qnx.sh base_unittests` progressed past the two shared-library link failures and exposed the next compile blocker in `base/synchronization/cancelable_event_default.cc`.

## Files touched

- `cef/patch/patch.cfg`
- `cef/patch/patches/qnx/chromium/qnx_linker_no_warn_execstack.patch`

## Related notes

- `docs/qnx/history/build-errors/compile/build-graph/qnx-is-linux-false-experiment-foundational.md`
