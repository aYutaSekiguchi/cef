# media/base/audio_parameters.h must gate is_always_lock_free on __cpp_lib_atomic_ref

- Date: 2026-06-08
- Signature: no member named 'is_always_lock_free' in 'std::atomic_ref<long>'
- Stage: compile
- Category: feature-guard
- Scope: media/base/audio_parameters.h cumulative_glitch fields

## Symptoms

- Clean QNX `out/qnx_release` builds stopped at step 1137/53138 with `ninja: build stopped: subcommand failed.`
- Five translation units in `chrome/services/speech/` and `chrome/services/media_gallery_util/` failed with the same diagnostic, all transitively including `media/base/audio_parameters.h`:
  - `obj/chrome/services/speech/lib/soda_speech_recognizer_impl.o`
  - `obj/chrome/services/media_gallery_util/public/mojom/mojom/media_parser.mojom.o`
  - `obj/chrome/services/speech/lib/speech_recognition_recognizer_impl.o`
  - `obj/chrome/services/speech/lib/audio_source_fetcher_impl.o`
  - `obj/chrome/services/speech/lib/speech_recognition_service_impl.o`
- The compiler emitted:
  ```
  ../../media/base/audio_parameters.h:60:43: error: no member named 'is_always_lock_free' in 'std::atomic_ref<long>'
     60 |   static_assert(std::atomic_ref<int64_t>::is_always_lock_free);
  ../../media/base/audio_parameters.h:62:44: error: no member named 'is_always_lock_free' in 'std::atomic_ref<unsigned long>'
     62 |   static_assert(std::atomic_ref<uint64_t>::is_always_lock_free);
  ```

## Root cause

- `media/base/audio_parameters.h` declares two `int64_t`/`uint64_t` cumulative-glitch counters in the shared-memory `AudioOutputBufferParameters` struct, and asserts that `std::atomic_ref<int64_t>::is_always_lock_free` (and the `uint64_t` counterpart) is `true` so the AudioService can update them lock-free.
- QNX SDP 8 libc++ does not provide standard-library `std::atomic_ref` at all, so the CEF QNX port force-includes `build/config/qnx/qnx_std_polyfill.h` (gated by `#if !defined(__cpp_lib_atomic_ref) || __cpp_lib_atomic_ref < 201806L`) which polyfills a minimal `std::atomic_ref<T>`.
- The polyfill deliberately does **not** define `is_always_lock_free` (and does not fake `__cpp_lib_atomic_ref`), consistent with the policy recorded in `v8-simdutf-atomic-base64-fallback.md`.
- Chromium 147's `audio_parameters.h` static_asserts therefore reference a member the polyfill does not expose, breaking every TU that includes the header on QNX.

## Fix pattern

- Mirror the `__cpp_lib_atomic_ref` capability check at the call site rather than expanding the polyfill or faking the capability macro. This is the same shape used by `v8_base64_atomic.patch` for the simdutf atomic Base64 path.
- Keep the polyfill minimal: it exists to satisfy code that **uses** `std::atomic_ref` operations (load/store/compare-exchange/fetch_add/fetch_or/required_alignment), not to advertise the full C++20 `is_always_lock_free` contract. Code that only needs lock-free primitives can stay polyfill-agnostic.
- Document the upstream file in a comment that points to the polyfill and to the existing precedent note so future maintainers do not "complete" the polyfill and accidentally hide the gap.

## Applied change

- Added `cef/patch/patches/qnx/chromium/media_audio_parameters_atomic_ref_qnx.patch` wrapping both `static_assert`s in:
  ```c
  #if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
    static_assert(std::atomic_ref<int64_t>::is_always_lock_free);
  #endif
    // ...
  #if defined(__cpp_lib_atomic_ref) && __cpp_lib_atomic_ref >= 201806L
    static_assert(std::atomic_ref<uint64_t>::is_always_lock_free);
  #endif
  ```
- Registered the patch in `cef/patch/patch.cfg` as `qnx/chromium/media_audio_parameters_atomic_ref_qnx`, placed immediately after `qnx/chromium/base_atomicops_qnx` to keep the `std::atomic_ref` family of fixes adjacent.
- The `base_atomicops_qnx` patch itself predates the polyfill and still works around `std::atomic_ref` directly via `alignof` / `reinterpret_cast<std::atomic<T>*>`. That workaround is now redundant with the polyfill and can be migrated in a follow-up; doing so is out of scope for this incident.

## Verification

- `git apply --check` and `git apply` both succeed against the current Chromium tree at the upstream `media/base/audio_parameters.h` revision pinned by `CHROMIUM_BUILD_COMPATIBILITY.txt`. The hunk anchor (`@@ -57,9 +57,19 @@ struct ... AudioOutputBufferParameters {`) and three-line context are stable against the small line shifts expected from Chromium 147 → 147.0.7727.147 rebase work.
- Re-running the build with the patch applied is expected to:
  - clear the five `FAILED:` lines for the `chrome/services/...` TUs,
  - leave `base/atomicops.cc` unchanged (the polyfill already supplies the symbols it uses),
  - leave all other Chromium-TUs unaffected (the `#if` is true on every non-QNX platform and is also true on QNX once a future SDP ships a libc++ that defines `__cpp_lib_atomic_ref`).

## Files touched

- `cef/patch/patches/qnx/chromium/media_audio_parameters_atomic_ref_qnx.patch`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/feature-guard/media-audio-parameters-stdatomic-ref-capability-gate.md`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/feature-guard/v8-simdutf-atomic-base64-fallback.md`
- `docs/qnx/history/build-errors/compile/toolchain-config/qnx-std-polyfill-newline-eof-warning.md`
- `cef/patch/patches/qnx/chromium/base_atomicops_qnx.patch` (predates the polyfill; redundant but kept until cleanup)
