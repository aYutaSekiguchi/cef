# Tradeoff Analysis: Providing `std::ranges::contains` on QNX

## 1. Context & Root Cause

**The in-tree libc++** (third_party/libc++, LLVM 22, C++23) provides `std::ranges::contains` (feature test macro `__cpp_lib_ranges_contains = 202207L`). This works fine on Linux, ChromeOS, Android, Mac, Windows — all of which use `use_custom_libcxx = true`.

**QNX does NOT use the in-tree libc++.** The QNX build config (`out/qnx_x64/args.gn`) sets:
```
use_custom_libcxx = false
```
Instead it uses `-I${_qnx_target}/usr/include/c++/v1` — the QNX SDK's bundled libc++. The QNX libc++ predates C++23 (or is compiled without C++23 mode), so `std::ranges::contains` is **not available**.

**Scale: 2638 total occurrences across 1378 unique files** (including 18 files in `base/`). This is not a small-scope problem.

---

## 2. Call-Site Patterns

### Pattern A: Simple range + value (≈2618 occurrences, the vast majority)
```cpp
std::ranges::contains(container, value)
// e.g.
if (std::ranges::contains(kAllowedSystemNotificationIDs, notification_id))
return std::ranges::contains(*list, value);
```
These are trivially replaceable with `absl::c_linear_search(container, value)`.

### Pattern B: Range + value + projection (≈20 occurrences)
```cpp
std::ranges::contains(container, value, &Type::member)
// e.g.
std::ranges::contains(accounts, account_id, &gaia::ListedAccount::id)
std::ranges::contains(owned_descriptors_, file, &base::ScopedFD::get)
```
These require more attention. `absl::linear_search` / `absl::c_linear_search` have **no projection support**. Each such call must be rewritten as either:
- An `absl::c_any_of` with a lambda: `absl::c_any_of(container, [&](const auto& x) { return x.id == account_id; })`
- Or a manual `std::find_if` loop

### Pattern C: std::initializer_list (used in `base/parameter_pack.h`)
```cpp
return std::ranges::contains(ilist, true);
```
`absl::c_linear_search` works here since initializer_list has `begin()`/`end()`.

---

## 3. Approach Comparison

### Approach A: Polyfill header with force-include

**Mechanism:** Create `base/containers/contains.h` with a trivial `contains()` wrapper, and force-include it for QNX builds via `build/config/compiler/BUILD.gn`.

**Implementation (very simple):**
```cpp
// base/containers/contains.h
#ifndef BASE_CONTAINERS_CONTAINS_H_
#define BASE_CONTAINERS_CONTAINS_H_

#include <algorithm>
#include <type_traits>

namespace base {

// Polyfill for std::ranges::contains when unavailable.
// Overload 1: range + value
template <std::ranges::input_range R, typename T>
constexpr bool contains(R&& range, const T& value) {
  return std::find(std::ranges::begin(range),
                   std::ranges::end(range), value)
         != std::ranges::end(range);
}

// Overload 2: range + value + projection
template <std::ranges::input_range R, typename T, typename Proj>
constexpr bool contains(R&& range, const T& value, Proj proj) {
  return std::find_if(std::ranges::begin(range),
                      std::ranges::end(range),
                      [&](const auto& elem) {
                        return std::invoke(proj, elem) == value;
                      })
         != std::ranges::end(range);
}

}  // namespace base
#endif
```

Wait — a force-include approach can't inject into `std::ranges`. It provides a different function. All call sites would still need to be rewritten, OR the force-include would need to inject into namespace std (UB).

**Correction:** A pure force-include approach can't transparently backport `std::ranges::contains`. You'd need to either:
1. Inject into `namespace std::ranges` via a header (technically UB, but feasible in practice)
2. Or rewrite all call sites to use a different function

**If we inject into `std::ranges`:** The polyfill header would go at the end of `<algorithm>` or be force-included. This is fragile and technically undefined behavior, though Chromium already does similar things (qnx_macros.h redefines MAP_ANONYMOUS, madvise, etc.).

**If we rewrite all call sites:** This is essentially the same as Approach B.

| Factor | Estimate |
|--------|----------|
| **Implementation effort** | ~2 hours for the polyfill header + build config change |
| **Call-site rewrite effort** | — (inherit from B unless injecting into `std::ranges`) |
| **Blast radius (build config)** | 1 file (`build/config/compiler/BUILD.gn`) + 1 new header |
| **Blast radius (source changes)** | 0 if injecting into std (UB); ~8-16 hours if rewriting |
| **Developer experience** | Transparent if injection works; "how does `std::ranges::contains` work on QNX?" magic if not |
| **Build system impact** | Adds compile-time overhead per translation unit (one more `-include`). Already precedented (QNX toolchain already has `-include time.h`). |
| **Testing implications** | No difference — same runtime behavior. Build breaks on QNX if injection order is wrong. |
| **Risk** | **HIGH** — injecting into `namespace std` is UB. Clang may accept it today and break tomorrow. Different QNX libc++ versions may conflict. |

**Verdict on Approach A:** Injecting into `std::ranges` is a ticking time bomb. Rewriting call sites to use a different function (even if defined in a force-included header) is just Approach B with extra ceremony. **Not recommended.**

---

### Approach B: Replace call sites with `absl::linear_search` / `absl::c_linear_search`

**Mechanism:** Search-and-replace all `std::ranges::contains(range, value)` with `absl::c_linear_search(range, value)`. For projection cases, rewrite using `absl::c_any_of` with a lambda.

**`absl::c_linear_search` already exists** in `third_party/abseil-cpp/absl/algorithm/container.h`. It takes a container and a value, exactly matching the common pattern.

**Existing usage:** Only 4 files in the codebase currently use `absl::linear_search` (mostly tests). It's available but not widely used.

| Factor | Estimate |
|--------|----------|
| **Implementation effort** | **~8-16 hours total** |
| | — 2 hours for mechanical replacement of ~2618 simple calls |
| | — 2-4 hours for ~20 projection calls (manual review + lambda rewrite) |
| | — 2 hours for build verification on QNX |
| | — 2-4 hours for test updates and fixups |
| **Blast radius (source changes)** | **~1378 files** — every file using `std::ranges::contains` |
| **Blast radius (build config)** | 0 — no build system changes needed |
| **Developer experience** | **Good.** `absl::c_linear_search` is self-documenting, lives in a well-known header, uses correct ADL/namespaces. No magic. Developers see exactly what function is being called. |
| **Build system impact** | **None.** Abseil is already a dependency. No new includes, no force-include overhead. |
| **Testing implications** | Behavior is identical: `std::find(...) != end()` either way. Projection rewrites need careful review but can be tested individually. |
| **Risk** | **LOW.** Mechanical change. Abseil is a first-party dependency. No UB. If the QNX libc++ later gains `std::ranges::contains`, call sites can be migrated back gradually. |

**Detailed rewrite rules:**

| Original | Replacement |
|----------|-------------|
| `std::ranges::contains(c, v)` | `absl::c_linear_search(c, v)` |
| `std::ranges::contains(c, v, &T::member)` | `absl::c_any_of(c, [&](const auto& x) { return x.member == v; })` |
| `std::ranges::contains(c, v, &T::get)` | `absl::c_any_of(c, [&](const auto& x) { return x.get() == v; })` |
| `std::ranges::contains(c, v, proj)` | `absl::c_any_of(c, [&](const auto& x) { return std::invoke(proj, x) == v; })` |
| `DCHECK(std::ranges::contains(...))` | `DCHECK(absl::c_linear_search(...))` |
| `EXPECT_TRUE(std::ranges::contains(...))` | `EXPECT_TRUE(absl::c_linear_search(...))` |

---

### Approach C: Hybrid — Small inline helper in a commonly-included header

**Mechanism:** Add a `base::contains()` function to an existing commonly-included header (e.g. `base/stl_util.h` or `base/compiler_specific.h`) and rewrite all call sites.

```cpp
// In base/stl_util.h
namespace base {

template <std::ranges::input_range R, typename T>
constexpr bool contains(R&& range, const T& value) { ... }

template <std::ranges::input_range R, typename T, typename Proj>
constexpr bool contains(R&& range, const T& value, Proj proj) { ... }

}  // namespace base
```

Then rewrite `std::ranges::contains(...)` to `base::contains(...)` everywhere.

| Factor | Estimate |
|--------|----------|
| **Implementation effort** | ~10-18 hours (same as B plus writing the helper) |
| **Blast radius** | 1378 files + 1 header change |
| **Developer experience** | **Good** — explicit namespace, no magic, easy to find definition. But it's Yet Another Utility Function. |
| **Build system impact** | Negligible (inline functions, no new includes beyond what's already in base/) |
| **Testing implications** | Same as B. Helper should have unit tests. |
| **Risk** | **LOW-MEDIUM.** ~2x implementation cost vs B. Slightly more maintainable long-term than B since the helper is Chromium-owned and supports projections. |

**Comparison with B:** Approach C gives you projection support in a straightforward way without `absl::c_any_of` lambdas. For ~20 projection call sites, that saves manual review time. However, writing and maintaining a `base::contains` adds its own cost.

---

## 4. Recommendation Summary

| Criterion | A (Polyfill + force-include) | B (absl::linear_search) | C (base::contains helper) |
|-----------|-----------------------------|--------------------------|---------------------------|
| Effort (hours) | 2–4 (if injecting into std) | 8–16 | 10–18 |
| Files changed | 1 build + 1 header | ~1378 source files | ~1378 source files + 1 header |
| Risk | **HIGH** (UB, fragile) | **LOW** | **LOW** |
| Developer DX | Poor (magic) | Good (explicit) | Good (explicit, owned) |
| Projection support | Yes | Via c_any_of lambdas | Native |
| Maintainability | Poor | Good | Best |
| Build impact | Non-zero (force-include) | Zero | Minimal |

**Recommended path:** **Approach B** (most pragmatic), or **Approach C** if you want ownership and native projection support.

**Avoid Approach A.** Injecting into `namespace std` is undefined behavior and creates a hidden dependency. The force-include mechanism already has precedent on QNX (`-include time.h`), but adding UB on top makes this the worst option.

### If choosing Approach B:

1. **Phase 1 (4 hours):** Write a script for the mechanical transformation:
   - `std::ranges::contains(container, value)` → `absl::c_linear_search(container, value)`
   - Apply across all 1378 files

2. **Phase 2 (2 hours):** Manually handle the ~20 projection call sites:
   - `std::ranges::contains(c, v, &T::member)` → `absl::c_any_of(c, [&](const auto& x) { return x.member == v; })`

3. **Phase 3 (2 hours):** Build on QNX, fix compile errors, fix tests

4. **Phase 4 (2 hours):** Add the PartitionAllocator PRESUBMIT exception for `std::ranges::contains` to the QNX-specific checks (or remove it from the general ban list)

### If choosing Approach C:

1. **Phase 1 (1 hour):** Add `base::contains()` to `base/stl_util.h`
2. **Phase 2–4:** Same as B phases 1–3, but transforming to `base::contains(...)` instead
3. **Phase 5 (1 hour):** Write unit tests for `base::contains()`

### Long-term note

Once QNX's SDK updates to a libc++ that includes C++23, the codebase can be migrated back to `std::ranges::contains` using the same automated approach. The `absl::c_linear_search` / `base::contains` wrappers have identical semantics.
