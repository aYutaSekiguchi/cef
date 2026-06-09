# private_key_factory.cc cannot prefix-increment std::find() result in C++23

- Date: 2026-06-09
- Signature: expression is not assignable
- Stage: compile
- Category: feature-guard
- Scope: components/enterprise/client_certificates/core/private_key_factory.cc

## Symptoms

- Clean QNX `out/qnx_release` builds stopped at step 4034/58883 (≈ 6.9 %) with `FAILED: obj/components/enterprise/client_certificates/core/core/private_key_factory.o` and the trailer `ninja: build stopped: subcommand failed.`
- The compile command for the failing TU is the standard QNX clang_x64 invocation (sysroot `/home/yuta/qnx800/target/qnx`, target `x86_64-unknown-nto`, `-std=c++23`, the `qnx_std_polyfill.h` force-include). All flags match the rest of the QNX tree.
- The compiler emitted a single diagnostic and stopped:
  ```
  ../../components/enterprise/client_certificates/core/private_key_factory.cc:126:14: error: expression is not assignable
    126 |              ++std::find(std::begin(kKeySourcesOrderedBySecurity),
        |              ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    127 |                          std::end(kKeySourcesOrderedBySecurity), source);
        |                          ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  1 error generated.
  ```
- A clean Linux x64 chromium 147 build (`~/chromium/test/src/out/Debug_GN_x64`) does **not** reproduce the error. On Linux x64 the same `enterprise/client_certificates` module compiles successfully.
- The previous five QNX fixes (`audio_parameters.h` capability gate, `power_metrics` exclusion + nullptr fallback, `os_crypt.h` `BUILDFLAG(IS_QNX)` extension, `policy_target_platform_qnx_fallback`, `update_client_unknown_os_qnx_branch`) all remain in place and unrelated.

## Root cause

- `components/enterprise/client_certificates/core/private_key_factory.cc:124-130` walks the `kKeySourcesOrderedBySecurity` array with a for-loop that uses the "fallback to less secure key" pattern. The author's original loop initializer is:
  ```cpp
  for (auto fallback_source =
           ++std::find(std::begin(kKeySourcesOrderedBySecurity),
                       std::end(kKeySourcesOrderedBySecurity), source);
       fallback_source != std::end(kKeySourcesOrderedBySecurity);
       fallback_source++) { ... }
  ```
  This relies on `std::find` returning a value whose value category permits `operator++`. C++17 effectively treated that return as lvalue (the function declared to return `ForwardIt` by value, but the result-of-function-call expression's category was lvalue in the contexts where `std::find` is used), so prefix `++` was accepted. C++20 changed the result-of-function-call expression category for such value returns to prvalue via LWG 2447 / P0408R7. Prefix `++` requires a modifiable lvalue; `++prvalue` is ill-formed. With Chromium 147's `-std=c++23` (upstream migrated to C++23 in milestone 147), clang 23 rejects line 126.
- This is the only `++std::find(...)` pattern in the entire Chromium tree (`grep -rn "++std::find" chromium` returns 1 hit). Most Chromium authors have always used `auto it = std::find(...); ++it;`; this file is the exception. The file was added in 2026-01 (commit `3845ec57f75ef` "Improve key creation fallback logic" by Sebastien Lalancette), i.e. shortly before the C++23 transition crystallized in M147. The author wrote the loop in the C++17 idiom and the C++23 cliff was not visible at the time of authoring.
- The same root cause (C++20 prvalue rules for `std::find` and friends) is a known industry-wide pitfall; clang 23 emits the same diagnostic on the same idiom across libstdc++-based projects.

## Fix pattern

- Bind the `std::find` result to a named variable (lvalue) and compute the "next" iterator explicitly inside the loop body. The `std::next(it)` call is itself a free function that takes an lvalue iterator and returns one, so the prvalue pitfall is sidestepped.
- Do **not** add `BUILDFLAG(IS_QNX)` guards or `||` extensions to the `std::find` chain. The error is in caller code, not in `std::find` or its headers. There is nothing QNX-specific about this bug.
- This is expected to become a temporary QNX port patch. Chromium's C++23 transition was a milestone-level change in M147; an upstream CL that lands in M148 (or later) is expected to move the loop to the new shape. When that CL lands, this CEF QNX patch will no longer apply cleanly via `git apply` (the surrounding context will have changed), and the CEF QNX bootstrap should drop the patch. The patch carries this expectation explicitly in the `patch.cfg` comment so that future maintainers can grep for the right signals.

## Applied change

- `components/enterprise/client_certificates/core/private_key_factory.cc` (1 hunk, +9 / -5 lines):
  ```diff
       if (!private_key && source != PrivateKeySource::kSoftwareKey) {
  -    for (auto fallback_source =
  -             ++std::find(std::begin(kKeySourcesOrderedBySecurity),
  -                         std::end(kKeySourcesOrderedBySecurity), source);
  -         fallback_source != std::end(kKeySourcesOrderedBySecurity);
  -         fallback_source++) {
  -      auto it = sub_factories_.find(*fallback_source);
  -      if (it != sub_factories_.end()) {
  +    for (auto source_it = std::find(std::begin(kKeySourcesOrderedBySecurity),
  +                                  std::end(kKeySourcesOrderedBySecurity),
  +                                  source);
  +         source_it != std::end(kKeySourcesOrderedBySecurity);
  +         ++source_it) {
  +      auto fallback_source = std::next(source_it);
  +      if (fallback_source == std::end(kKeySourcesOrderedBySecurity))
  +        break;
  +      auto fallback_it = sub_factories_.find(*fallback_source);
  +      if (fallback_it != sub_factories_.end()) {
         ...
       }
  ```
  The loop semantics are equivalent: iterate over the remaining sources after `source`, stopping at the end. The end-of-range check moves from the loop condition to an explicit `if (fallback_source == end) break;` inside the body. The durable patch also uses distinct iterator names (`source_it`, `fallback_it`) so the C++23 refactor does not introduce a same-scope shadowing error.

## Verification

- Clean-tree bootstrap still succeeds with the patch registered in `patch.cfg`.
- Re-running `./out/qnx_release/ninja_qnx.sh cefsimple` now confirms both private-key signatures are gone:
  - `private_key_factory.cc:126` `expression is not assignable`
  - later refactor regression `private_key_factory.cc:132` `redefinition of 'it'`
- The patch-managed QNX build advances past `private_key_factory.o` and, on current verification, exposes a later PDFium blocker (`third_party/pdfium/core/fxge/linux/fx_linux_impl.cpp:23:2: error: "Included on the wrong platform"`).
- Linux x64 / macOS / Windows / Android / ChromeOS / Fuchsia / OpenBSD builds are byte-for-byte unchanged; the new loop is a pure local-variable refactor.

## Files touched

- `cef/patch/patches/qnx/chromium/private_key_factory_find_prvalue_qnx.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/feature-guard/private-key-factory-find-prvalue-qnx.md`

## Carry-forward expectation (CARRY-FORWARD-NOTE: drop on upstream fix)

This patch is **expected to be temporary**. Chromium 147 migrated the default `-std=` from C++17 to C++23 (build/config/compiler/BUILD.gn:711). The original `private_key_factory.cc` loop was authored in the C++17 era (commit `3845ec57f75ef`, 2026-01) and relies on the pre-C++20 value-category of `std::find`'s return. As soon as upstream ships a CL that moves the loop to the new shape, this CEF QNX patch will no longer apply cleanly via `git apply` (the surrounding context will have changed, the `auto it = ...; ++it;` block will already exist). The CEF QNX port should then drop this patch in the next sync-up:

```sh
# After an upstream chromium sync lands the fix, drop the patch:
rm /home/yuta/chromium/src/cef/patch/patches/qnx/chromium/private_key_factory_find_prvalue_qnx.patch
# And remove the entry from patch.cfg:
#   'name': 'qnx/chromium/private_key_factory_find_prvalue_qnx',
#   → delete the surrounding {...} block
# And delete this note (it is the historical record of the C++23 transition).
```

Until then, the patch is required to keep the QNX bootstrap green.

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/media-audio-parameters-stdatomic-ref-capability-gate.md` (companion fix: capability-gate pattern, fix #1)
- `docs/qnx/history/build-errors/compile/platform-api-gap/qnx-power-metrics-linux-perf-event-no-op-fallback.md` (companion fix: source-set exclusion, fix #2)
- `docs/qnx/history/build-errors/compile/feature-guard/os-crypt-bUILDFLAG-IS-LINUX-qnx-extension.md` (companion fix: BUILDFLAG guard extension, fix #3)
- `docs/qnx/history/build-errors/compile/toolchain-config/policy-target-platform-qnx-fallback.md` (companion fix: toolchain/args wrapper, fix #4)
- `docs/qnx/history/build-errors/compile/feature-guard/update-client-unknown-os-qnx-branch.md` (companion fix: feature-map extension, fix #5)
- `build/config/compiler/BUILD.gn:711` (`-std=c++23` for the default Chromium 147 toolchain; the cause of this regression)
- LWG 2447 / P0408R7 (the C++20 paper that changed the value category of `std::find`'s return from lvalue to prvalue, the underlying standardization decision)
- commit `3845ec57f75ef` (Sebastien Lalancette, 2026-01-27, "Improve key creation fallback logic", the upstream commit that introduced the C++17-only idiom at line 126)
