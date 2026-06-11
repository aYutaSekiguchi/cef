# QNX SwiftShader tests were missing from `gn_all`, so `gn ls` could not see the test binaries

- Date: 2026-06-09
- Signature: `gn desc out/qnx_release //third_party/swiftshader:swiftshader_tests` matched no targets; `gn ls out/qnx_release | grep swiftshader` showed only non-test SwiftShader refs
- Stage: gn
- Category: build-graph
- Scope: `BUILD.gn` root `gn_all`, `third_party/swiftshader` test targets

## Symptoms

- A clean QNX graph generation completed, but the SwiftShader QNX test binaries were absent from the visible target set.
- `gn ls out/qnx_release | grep swiftshader` did not show the SwiftShader test binaries.
- `gn ls out/qnx_release '//third_party/swiftshader/tests/*'` returned only after the fix; once the LLVM support flag was restored, all three test binaries became visible.

## Root cause

- The root `group("gn_all")` listed ANGLE tests, but it did not depend on SwiftShader's `swiftshader_tests` group.
- GN therefore never pulled `third_party/swiftshader/BUILD.gn` and its `tests/*/BUILD.gn` files into the QNX graph.
- The QNX test runner had binary names for the SwiftShader suite, but the graph never exposed those binaries to Ninja.

## Fix pattern

- When a QNX test module expects prebuilt binaries, make sure the root GN graph actually reaches the test targets.
- Prefer adding the upstream test group to `gn_all` instead of hard-coding individual binaries in multiple places.

## Applied change

- Added `"//third_party/swiftshader:swiftshader_tests"` to the root `group("gn_all")` on QNX.
- Restored SwiftShader's LLVM test eligibility with `supports_llvm = is_qnx || ...`.
- Registered the changes as `cef/patch/patches/qnx/chromium/gn_all_swiftshader_tests_qnx.patch` and `cef/patch/patches/qnx/chromium/swiftshader_qnx_llvm_support.patch`.

## Verification

- `gn gen out/qnx_release` succeeded.
- `gn ls out/qnx_release '//third_party/swiftshader/tests/*'` now lists:
  - `swiftshader_system_unittests`
  - `swiftshader_system_unittests__runner`
  - `swiftshader_reactor_llvm_unittests`
  - `swiftshader_reactor_llvm_unittests__runner`
  - `swiftshader_reactor_subzero_unittests`
  - `swiftshader_reactor_subzero_unittests__runner`
- `gn ls out/qnx_release | rg 'swiftshader'` now includes the full SwiftShader test set.

## Files touched

- `BUILD.gn`
- `cef/patch/patches/qnx/chromium/gn_all_swiftshader_tests_qnx.patch`
- `cef/patch/patch.cfg`

## Related notes

- `docs/qnx/history/build-errors/compile/platform-api-gap/swiftshader-llvm-elf-macro-pollution.md`
- `docs/qnx/build-error-index.md`
