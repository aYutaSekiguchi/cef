# Review: Approaches for `std::ranges::contains` on QNX — Maintenance & Long-Term Impact

## Table of Contents
1. [Executive Summary](#executive-summary)
2. [Investigated Evidence](#investigated-evidence)
3. [Approach A: Polyfill Header (Force-Include via Toolchain)](#approach-a-polyfill-header-force-include-via-toolchain)
4. [Approach B: Replace Call Sites with `absl::linear_search`](#approach-b-replace-call-sites-with-absl-linear_search)
5. [Cross-Cutting Concerns](#cross-cutting-concerns)
6. [Recommendation](#recommendation)

---

## Executive Summary

**Neither approach is ideal**, but Approach A (polyfill header) is significantly better for long-term maintenance. Approach B would be a regression — trading a compile-time gap for a reading/maintenance tax on every single one of 191 call sites across 143 files, forever. Below is the detailed analysis.

---

## Investigated Evidence

### 1. Scale of usage
- **191 call sites** of `std::ranges::contains` across **143 files** in `cc/`, `content/`, `url/`, `printing/`, `remoting/`, and more.
- This is not a fringe feature; it is the idiomatic C++23 way to check membership in a range.

### 2. Chromium ships its own libc++
- `build/config/c++/BUILD.gn` sets `-nostdinc++` and adds Chromium's custom libc++ via `-isystem` paths.
- `third_party/libc++/src/include/__algorithm/ranges_contains.h` **exists** and implements `std::ranges::contains` (behind `_LIBCPP_STD_VER >= 23`).
- `__config_site` (at `buildtools/third_party/libc++/__config_site`) has no QNX-specific overrides.
- Chromium builds with `-std=c++23` by default.

### 3. Why QNX breaks
The QNX toolchain (`build/toolchain/qnx/BUILD.gn`) adds:
```gn
extra_cxxflags = "... -I${_qnx_target}/usr/include/c++/v1 -include time.h"
```

In Clang/GCC, **`-I` directories are always searched before `-isystem` directories**, regardless of flag ordering. This means the QNX system libc++ headers **shadow** Chromium's custom libc++ headers. The older QNX libc++ likely lacks `std::ranges::contains` (added in C++23 / libc++ 16).

### 4. QNX existing patterns
- `qnx_macros.h` defines `ElfW()`, `__THROW`, `MAP_ANONYMOUS`, `madvise`, etc. — **all manual `#include`** in individual source files. Not force-included.
- Only `time.h` is currently force-included (via `-include time.h` in the toolchain).
- No existing force-include pattern for standard library polyfills.

### 5. `absl::linear_search` implementation
```cpp
template <typename InputIterator, typename EqualityComparable>
bool linear_search(InputIterator first, InputIterator last,
                   const EqualityComparable& value) {
  return std::find(first, last, value) != last;
}
```
It is a simple iterator-pair interface, **not** range-based. It lacks projector support.

### 6. Other Chromium platforms
- No other platform (Fuchsia, iOS, macOS, Android, Windows) has a similar workaround for `std::ranges::contains`.
- No `base/containers/contains.h` wrapper exists in Chromium today.

---

## Approach A: Polyfill Header (Force-Include via Toolchain)

### How it would work
1. Create a header (e.g., `build/config/qnx/qnx_ranges_contains.h`) that conditionally defines `std::ranges::contains` when the feature test macro `__cpp_lib_ranges_contains` is not defined (or when the QNX libc++ version is detected).
2. Add `-include build/config/qnx/qnx_ranges_contains.h` to the QNX toolchain's `extra_cxxflags`.

### Interaction with a future QNX libc++ update
- **Good.** The header already checks `__cpp_lib_ranges_contains`. When QNX eventually ships a libc++ that provides `std::ranges::contains`, the feature test macro will be defined by the system headers and the polyfill will be a no-op automatically.
- The `std::ranges::contains` CPO (customization point object) is defined as an inline `constexpr` variable in `namespace ranges::__cpo`. If the real libc++ provides it, the polyfill must not conflict. The guard (`#ifndef __cpp_lib_ranges_contains`) handles this transparently.
- **Risk:** If QNX ships a partial implementation that defines `__cpp_lib_ranges_contains` but has bugs, the polyfill would be skipped. This is an inherent risk with any feature-test-macro-based approach.

### Interaction with an upstream Chromium merge
- **Good, but requires care.** If Chromium upstream changes the signature of `std::ranges::contains` (unlikely for a standard algorithm) or the internal `__cpo` namespace structure, the polyfill would become stale. However, since this polyfill mirrors the standard, the risk is minimal.
- The polyfill lives in `build/config/qnx/`, which is a QNX-specific directory. Upstream merges that touch QNX files are rare and deliberate. The polyfill won't cause merge conflicts in mainline (non-QNX) files.

### Migration cost when removing
- **Very low.** Delete one file (`qnx_ranges_contains.h`) and remove one `-include` flag from the toolchain `BUILD.gn`. Zero source files to change.

### Self-documenting vs. hidden
- **Moderately hidden.** With a force-include, developers looking at `std::ranges::contains` usage won't see where it comes from. They'd need to know about the QNX toolchain flags or search for the polyfill header.
- Mitigation: A comment in the header like `// Polyfill for QNX libc++ which lacks std::ranges::contains (C++23)` makes the intent clear to anyone who finds the file.
- The `__cpp_lib_ranges_contains` guard is self-documenting for readers familiar with feature test macros.

### Consistency with existing QNX patterns
- The current `-include time.h` pattern in the QNX toolchain already establishes force-include as a valid QNX mechanism. This approach extends that existing pattern.

### Verdict
| Criterion | Score |
|---|---|
| Future QNX libc++ update | Good — automatic no-op via feature test macro |
| Upstream merge conflict risk | Low — isolated to QNX-specific files |
| Migration cost on removal | Very low — delete 1 file, remove 1 flag |
| Self-documenting | Moderate — hidden from call sites, clear at definition |
| Consistency with existing patterns | Good — follows `-include time.h` pattern |

---

## Approach B: Replace Call Sites with `absl::linear_search`

### How it would work
1. Change all 191 `std::ranges::contains(...)` call sites to `absl::linear_search(...)`.
2. For range-based calls (e.g., `std::ranges::contains(vec, value)`), convert to `absl::linear_search(vec.begin(), vec.end(), value)`.

### Interaction with a future QNX libc++ update
- **Poor.** When QNX eventually provides `std::ranges::contains`, every single call site would still use `absl::linear_search`. There is no automatic migration path.
- A follow-up cleanup would need to individually revert 191+ call sites back to `std::ranges::contains`. This is a massive, tedious, error-prone refactor.

### Interaction with an upstream Chromium merge
- **Very poor.** Every upstream CL that adds a new `std::ranges::contains` call will cause a merge conflict in the QNX branch. The QNX branch would need to manually convert each new call site to `absl::linear_search`.
- Upstream Chromium actively uses C++23 features; new `std::ranges::contains` calls will regularly appear. Each new call is a perpetual merge burden.

### Migration cost when removing
- **Very high.** Need to touch 143 files. Each call site must be individually reviewed to ensure the semantics match. `std::ranges::contains` and `absl::linear_search` have different interfaces:
  - `std::ranges::contains(range, value)` — range-based, single argument
  - `absl::linear_search(first, last, value)` — iterator-pair, different semantics for associative containers
  - `std::ranges::contains` supports a **projector** argument (`std::ranges::contains(vec, value, &Widget::id)`). `absl::linear_search` does not. Projector-using calls cannot be trivially replaced.

### Projector incompatibility (critical)
Several existing uses pass a projector:
```cpp
// cc/trees/damage_tracker.cc
std::ranges::contains(previous_view_transition_content_surfaces_by_id_, key,
                      &decltype(current_view_transition_content_surfaces_by_id_)::value_type::first);
```
`absl::linear_search` has **no projector parameter**. These calls would need different workarounds (e.g., `std::ranges::find` + comparison). This makes Approach B incomplete — additional effort per call site.

### Self-documenting vs. hidden
- **Clear, but misleading.** `absl::linear_search` is visible at every call site, which is good. However, it conveys a false intent: "this code path is taken because absl was chosen algorithmically." In reality, it's a workaround for a missing C++23 feature.
- New contributors may wonder "why `absl::linear_search` here and `std::ranges::contains` there?", creating confusion.

### Consistency with existing patterns
- **Inconsistent.** Chromium's codebase standard is `std::ranges::contains`. Introducing `absl::linear_search` as a QNX-only alternative creates a permanent fork in coding style within the same codebase.

### Verdict
| Criterion | Score |
|---|---|
| Future QNX libc++ update | Poor — no automatic migration; massive cleanup needed |
| Upstream merge conflict risk | Very poor — every new `std::ranges::contains` call conflicts |
| Migration cost on removal | Very high — 143 files, 191+ sites, projector incompatibility |
| Self-documenting | Moderate — visible but misleading |
| Consistency with existing patterns | Poor — creates permanent style fork |

---

## Cross-Cutting Concerns

### 1. The root cause
The QNX toolchain adds `-I${_qnx_target}/usr/include/c++/v1`, which shadows Chromium's custom libc++. **A better long-term fix might be to fix this include path issue** — e.g., ensuring Chromium's `-isystem` paths precede QNX's `-I` path, or converting the QNX include to `-isystem`. However, this may break other things if QNX headers are needed for platform-specific features.

### 2. Missing feature, not just `contains`
If QNX uses its own libc++ instead of Chromium's custom one, it will lack **all** C++23 features, not just `std::ranges::contains`. A polyfill approach scales better: a single `qnx_ranges_contains.h` is deployable now, and additional polyfills can be added to the same pattern as other gaps emerge.

### 3. Neither approach addresses the projector gap
Consider a `base::ranges::contains` wrapper (in `base/containers/`) that:
- Uses `std::ranges::contains` when available (C++23)
- Falls back to a hand-rolled implementation when not

This would provide a **single abstraction layer** that works on all platforms. Chromium already does this for other features (e.g., `base::Contains` doesn't exist, but `base::flat_map` and friends provide their own). However, introducing this now would be a significant new API surface — a separate decision.

### 4. `absl::linear_search` is not available in all contexts
Check whether `absl::algorithm/algorithm.h` is already included in all the translation units that use `std::ranges::contains`. Forcing QNX to include absl headers may introduce dependency issues or increase compile times.

---

## Recommendation

**Adopt Approach A: Polyfill header force-included via toolchain.**

Rationale:
- **Lowest migration cost** (delete 1 file when QNX catches up).
- **No merge conflicts** with upstream Chromium.
- **Handles the projector gap gracefully** — the polyfill can implement the full `std::ranges::contains` CPO with projector support, matching the standard interface exactly.
- **Scales** to additional missing features as they are discovered.
- **Already has precedent** in the `-include time.h` pattern in the same toolchain file.

However, add these mitigations for the "hidden" concern:
1. Add a clear comment in the polyfill header: `// QNX polyfill: std::ranges::contains (missing from QNX libc++ xxx)`
2. Add a `#warning` or `#pragma message` when the polyfill is active, so developers building QNX see a build-time note.
3. Track the fixup in a bug or README so removal is not forgotten when QNX updates.

If resources permit and the platform team prefers **maximum explicitness**, the ideal solution (orthogonal to A vs. B) is:
- Define `base::Contains(range, value)` / `base::Contains(range, value, proj)` in `base/containers/contains.h` that delegates to `std::ranges::contains` on all platforms, with a QNX fallback.
- This is a larger effort but gives the best of both worlds: visible, single migration point, and no toolchain magic.
