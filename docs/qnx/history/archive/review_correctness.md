# Review: Approaches for `std::ranges::contains` on QNX

## 1. Scale: "29 call sites" is a dramatic underestimate

| Category | Count (non-test `.cc`/`.h`) |
|---|---|
| Total call sites | **~1745** |
| Non-projection (simple value) | **~1700** |
| Projection-based (`&Class::member`) | **~45** |

Every one of these would be affected by any replacement strategy. The "29" figure is wrong by ~60× for non-test code alone.

---

## 2. Semantic signature analysis

### `std::ranges::contains` (C++23, P2302R1)

```cpp
// (1) iterator-pair overload
template<ranges::input_iterator I, ranges::sentinel_for<I> S, class T,
         class Proj = std::identity>
constexpr bool contains(I first, S last, const T& value, Proj proj = {});

// (2) range overload  ← Chromium uses this form almost exclusively
template<ranges::input_range R, class T, class Proj = std::identity>
constexpr bool contains(R&& r, const T& value, Proj proj = {});
```

Returns `ranges::find(std::forward<R>(r), value, proj) != ranges::end(r)`.

### `absl::linear_search` (iterator-pair only, no projections)

```cpp
template <typename InputIterator, typename EqualityComparable>
constexpr bool linear_search(InputIterator first, InputIterator last,
                             const EqualityComparable& value);
```

### `absl::c_contains` (range-compatible, no projections)

```cpp
template <typename Sequence, typename T>
constexpr bool c_contains(const Sequence& sequence, T&& value);
```

Returns `absl::c_find(sequence, std::forward<T>(value)) != c_end(sequence)`.

### Semantic gap summary

| Feature | `std::ranges::contains` | `absl::linear_search` | `absl::c_contains` |
|---|---|---|---|
| Range overload | ✅ | ❌ | ✅ |
| Projection (`Proj`) | ✅ | ❌ | ❌ |
| `constexpr` | ✅ (C++23) | ✅ (C++20) | ✅ (C++20) |
| Equality via `==` | ✅ | ✅ | ✅ |
| Forwarding ref for value | ✅ (const T&) | ✅ (const&) | ✅ (T&&) |
| Sentinel support | ✅ | ❌ (iterator pair) | ❌ (range only) |

**Bottom line:** `absl::linear_search` is **not** an exact semantic match — it lacks both the range overload and projection support. `absl::c_contains` adds the range overload but still lacks projection support.

---

## 3. Evaluation of Approach A: Namespace-std polyfill via force-include

### Proposal
Create `build/config/qnx/qnx_std_polyfill.h`, force-include via QNX toolchain's `-include` flag (same mechanism already used for `time.h`). Zero source changes.

### ✅ Strengths
1. **Precedent established.** The QNX toolchain already uses `-include time.h` (in `build/toolchain/qnx/BUILD.gn`). Adding another `-include` is mechanically identical.
2. **Zero source changes.** No need to modify ~1745 call sites or ~45 projection calls.
3. **Future-proof structure.** When QNX libc++ eventually adds `std::ranges::contains`, the polyfill can be gated behind a version check and removed cleanly from one file.
4. **No project-wide include changes.** Files that do not currently include `<ranges>` won't be affected (the polyfill only needs to be visible when `<ranges>` is already included).

### ⚠️ ODR violation risk (namespace std)

**The standard says:** Adding declarations/functions to `namespace std` is **undefined behavior** ([[namespace.std]/1](https://eel.is/c++draft/namespace.std#1)), with narrow exceptions for template specializations of standard library templates on user-defined types.

**In practice:**
- The UB here is a "language-lawyer" violation — no real ODR conflict can arise on QNX because the library does not define `std::ranges::contains`, so there is no duplicate symbol.
- This technique is widely used by polyfill libraries (e.g., Android's libc++ has done similar things, abseil documents its `c_contains` as the "absl version of std::ranges::contains").
- The practical risk is not ODR today, but **incorrect gating** when QNX libc++ eventually gets the function.

### 🚩 Critical: Preprocessor detection correctness

The feature-test macro is `__cpp_lib_ranges_contains` with value `202207L`. The polyfill MUST be gated:

```cpp
// qnx_std_polyfill.h — only for QNX, only when library lacks it
#if defined(__QNX__) && !defined(__cpp_lib_ranges_contains)
#include <ranges>  // ensure ranges namespace exists

namespace std::ranges {
// ... polyfill
}
#endif
```

**Risks with this gate:**
1. **QNX libc++ may define `__cpp_lib_ranges_contains` to `0`** pre-release, then later to `202207L`. A check for `__cpp_lib_ranges_contains < 202207L` is safer than `!defined(...)`.
2. **If QNX ships a partial backport**, the polyfill and the library definition could collide. The `-include` runs before any user code, but after built-in includes — so if `<ranges>` (included transitively by user code) provides `contains`, the polyfill (included before) is fine. But if the polyfill's `#include <ranges>` pulls in a conflicting definition, that's a redefinition error.
3. **Testing gap:** This polyfill path only runs on QNX. CI without QNX hardware/emulation cannot catch breakage.

### 🚩 Compatibility when QNX libc++ eventually adds it

Two scenarios:

| Scenario | Impact |
|---|---|
| QNX adds `contains` but removes `-include` lags | **Compiler error:** redefinition of `std::ranges::contains`. Immediately visible in build. |
| QNX adds `contains` and `-include` is removed | **No impact.** Code compiles against the real implementation. |

The phase-out mechanism requires:
- Updating the gate condition in the polyfill header (easy).
- Removing the `-include` flag from the toolchain definition (easy).
- Testing on the new QNX SDK.

**No ABI impact** — `contains` is a `constexpr` function template, always inline, never exported from a library.

---

## 4. Evaluation of Approach B: Replace all call sites with `absl::linear_search`

### Proposal
Replace every `std::ranges::contains(r, value)` with `absl::linear_search(std::begin(r), std::end(r), value)`.

### ❌ Blocker: 45 projection-based calls cannot migrate to `absl::linear_search`

`std::ranges::contains(r, value, &Class::member)` has **no direct equivalent** in `absl::linear_search`. Projection is a fundamentally different interface. Options:

| Option | Drawback |
|---|---|
| `absl::c_any_of(r, [&](const auto& e) { return e.*member == value; })` | Verbose, introduces lambda at every call site. Must ensure `std::ranges::any_of` is available (it's C++20 — may be present on QNX 18.1, but not guaranteed). |
| Write a custom `contains_proj` helper | Defeats the purpose of "zero new code" and creates a bespoke API inconsistent with other call sites. |
| Use `absl::c_contains` (which also lacks projections) | Same problem — projections remain unsupported. |

### 🚩 1700+ non-projection call sites also problematic

Even for simple calls, replacement is not trivial:
- **Some call sites use rvalue ranges** (e.g., `std::ranges::contains(func_returning_vec(), value)`) — `absl::linear_search` needs an lvalue range, so `std::begin`/`std::end` must be on the rvalue directly (works but fragile).
- **Some call sites chain `contains` with other range operations** — the substitution may not be mechanical (`contains` in an expression context).
- **Risk of missing call sites.** With 1745+ call sites across the tree, a search-and-replace will miss some, causing silent link failures on QNX only (since other platforms have the real `contains`).
- **Includes must be updated** everywhere — each file needs `#include "absl/algorithm/algorithm.h"` (or `container.h`).

### ✅ Avoids namespace-std UB
No ODR violation concerns. No undefined behavior. Clean separation from the standard library.

---

## 5. Detailed risk comparison

| Risk | Approach A (polyfill header) | Approach B (absl migration) |
|---|---|---|
| **ODR / UB in namespace std** | 🔴 Standard says UB; low practical risk with proper gating | 🟢 None |
| **Projection support** | 🟢 Fully preserved | 🔴 **Not supported** — blocks migration of ~45 call sites |
| **Source change scope** | 🟢 Zero source changes | 🔴 ~1745+ call sites + include additions |
| **Patch size** | 🟢 ~20 lines in one header file | 🔴 Thousands of lines diff across 100+ files |
| **Merge conflict risk** | 🟢 Negligible (one file) | 🔴 High (touches actively developed code) |
| **Future compatibility** | 🟢 Gated; easy to remove | 🟢 Works regardless of libc++ version |
| **Testing burden** | 🟡 Only QNX-specific; hard to CI-test | 🟢 Compiles everywhere, testable in existing CI |
| **Regression risk** | 🟡 Potential if gate logic is wrong | 🟢 Semantically equivalent (for non-projection) |
| **Build-time impact** | 🟢 None (single header) | 🟡 Slightly slower (more includes, more source files touched) |

---

## 6. Precedent: existing QNX force-includes

Current `build/toolchain/qnx/BUILD.gn`:

```gn
extra_cflags     = "${_target_flags} -include time.h"
extra_cppflags   = "${_target_flags} -include time.h"
extra_cxxflags   = "${_target_flags} -I${_qnx_target}/usr/include/c++/v1 -include time.h"
```

`-include time.h` is already there for QNX-specific workarounds. Adding `-include build/config/qnx/qnx_std_polyfill.h` follows the exact same pattern. The `-include` flag is a well-supported Clang feature.

---

## 7. Recommended approach (hybrid)

Based on the evidence, **neither pure approach is ideal alone.** A hybrid is best:

1. **Create `build/config/qnx/qnx_std_polyfill.h`** that polyfills `std::ranges::contains` (and optionally `contains_subrange`) inside `namespace std::ranges`, gated on `!defined(__cpp_lib_ranges_contains)`.
   - Supports call sites with and without projections.
   - Zero source changes.
   - Follows existing QNX toolchain pattern.

2. **Use the feature-test macro correctly:**
   ```cpp
   #if defined(__QNX__) && \
       (!defined(__cpp_lib_ranges_contains) || __cpp_lib_ranges_contains < 202207L)
   ```

3. **Plan for removal:** When QNX SDK upgrades to a libc++ that provides `contains`, the gate condition silently stops applying. The header and `-include` flag should be cleaned up at that point.

4. **Do NOT attempt approach B** (wholesale `absl::linear_search` replacement) — it is impractical at scale, incompatible with projections, and touches too many files for too little benefit.

---

## 8. Hidden pitfalls

### `constexpr` on QNX libc++ 18.1
`std::ranges::contains` is `constexpr` in C++23. The polyfill must also be `constexpr` to preserve compile-time evaluation. `absl::linear_search` already has `ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20`, so this works. A namespace-std polyfill should use plain `constexpr` (no Abseil macros in namespace std).

### Range adaptation / views
Some call sites may pass range adaptors (e.g., `container | views::filter(...)`) to `contains`. The polyfill's template parameter `R` must accept any `ranges::input_range`. Use `std::forward<R>(r)` to preserve value category, matching the standard signature.

### Opaque sentinels
`std::ranges::contains`'s iterator-pair overload uses `ranges::sentinel_for<S, I>`, not `same_as<I>`. The polyfill's range-based overload internally calls `ranges::begin(r)` and `ranges::end(r)` — if these are different types, the polyfill's implementation must call `ranges::find(r, value, proj)` (which handles sentinel types), not a manual loop.

### Conflicting `-include` ordering
If QNX's system headers already `-include` something, the polyfill header must be compatible with that order. The polyfill should be self-contained (include `<ranges>` and `<algorithm>` itself) and not depend on earlier includes.

### `absl::c_contains` already exists as a documented alternative
Abseil explicitly documents `absl::c_contains` as "Container-based version of the `<algorithm>` `std::ranges::contains()` C++23 function". If a slower, project-wide migration is ever desired, use `absl::c_contains` (not `absl::linear_search`) — it at least has the range overload. But it still lacks projection support.

---

## 9. Verdict

| Dimension | Verdict |
|---|---|
| **Approach A feasibility** | ✅ **Feasible and recommended** with proper feature-test gating |
| **Approach B feasibility** | ❌ **Not feasible** — broken by projections, impractical at ~1745 call sites |
| **Blocker found** | Approach B cannot handle ~45 projection-based call sites |
| **Lowest-risk path** | Approach A hybrid: one polyfill header + existing `-include` mechanism |
| **Primary remaining risk** | Correct `__cpp_lib_ranges_contains` gating when QNX SDK updates |
