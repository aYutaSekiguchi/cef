# content/test/BUILD.gn `data` variable fix investigation

## Problem Summary

### Root Cause
In `content/test/BUILD.gn`, the variable `data` is conditionally defined at line 3230 within an `if` block:

```gn
# Line 3229-3237
if (is_android || is_linux || is_chromeos || is_mac || is_win || is_fuchsia) {
  data = [
    "$root_out_dir/content_shell.pak",
    "data/",
    "//content/test/data/attribution_reporting/databases/",
    "//content/test/data/browsing_topics/",
    "//content/test/data/btm/",
    "//media/test/data/",
  ]
}
```

**QNX (`is_qnx`) is missing from this condition.** On QNX, `data` remains undefined.

At line 3456, the code attempts:

```gn
data += [
  "$root_gen_dir/third_party/perfetto/protos/perfetto/config/chrome/scenario_config.descriptor",
  "$root_gen_dir/third_party/perfetto/protos/perfetto/config/config.descriptor",
]
```

This causes: `Undefined identifier 'data'` on QNX builds.

## All `data +=` usages in content/test/BUILD.gn

| Line | Context | Depends on line 3230? |
|------|---------|----------------------|
| 927  | `fuchsia_telemetry_test_data` group | No (different target) |
| 932  | Same target | No |
| 940  | Same target | No |
| 2094 | `content_browsertests` target | **No** (has unconditional `data = [...]` at 2083) |
| 2150 | Same target | **Yes** (inside `if (is_android \|\| is_linux \|\| is_chromeos \|\| is_mac \|\| is_win)` at 2139) |
| 2169 | Same target | **Yes** |
| 2319 | Same target | **Yes** |
| 2371 | Same target | **Yes** |
| 3456 | `content_unittests` target | **Yes** |

### Key distinction: Two different targets

1. **`content_browsertests`** (starts ~line 2083): Has unconditional `data = [...]` initialization at line 2083, before any `if` blocks. Safe for all platforms.

2. **`content_unittests`** (starts ~line 2750+): Relies solely on the `data` definition at line 3230. **This is the problematic target.**

## Fix Options

### Option A: Add `is_qnx` to the existing condition (Recommended)

**File:** `content/test/BUILD.gn`  
**Lines:** 3229-3237

```diff
-  if (is_android || is_linux || is_chromeos || is_mac || is_win || is_fuchsia) {
+  if (is_android || is_linux || is_chromeos || is_mac || is_win || is_fuchsia || is_qnx) {
     data = [
       "$root_out_dir/content_shell.pak",
       "data/",
       "//content/test/data/attribution_reporting/databases/",
       "//content/test/data/browsing_topics/",
       "//content/test/data/btm/",
       "//media/test/data/",
     ]
   }
```

**Pros:**
- Single-line change
- Explicit platform enumeration maintained
- Follows existing pattern exactly
- No risk of runtime `data` being empty on QNX

**Cons:**
- Need to update if QNX doesn't need these test data files

### Option B: Add empty `data = []` before the conditional

**File:** `content/test/BUILD.gn`  
**Before line 3229:**

```diff
+  data = []

   if (is_android || is_linux || is_chromeos || is_mac || is_win || is_fuchsia) {
     data = [
```

**Pros:**
- `data` always defined regardless of platform
- More defensive approach

**Cons:**
- Semantically unclear why we need empty list for QNX
- May include unnecessary empty data dependencies

## Recommendation

**Option A** is preferred because:

1. It follows Chromium's convention of explicitly listing supported platforms
2. Clear intent: QNX tests should have the same test data as other platforms
3. Minimal change surface
4. If QNX doesn't need these files, an explicit QNX-specific empty block would be cleaner than defaulting to all platforms

## Trade-off Analysis

| Aspect | Option A | Option B |
|--------|----------|----------|
| Simplicity | ✅ Single condition edit | Adds extra line |
| Clarity | ✅ Explicit QNX support | ❌ Silent empty list |
| Maintainability | ✅ Platform list in one place | ❌ Data split across conditions |
| Risk | Low | Low |

## Related Findings

1. **No `assert(data, ...)` found** in `content/test/BUILD.gn` — the issue is pure GN undefined identifier, not assertion.

2. **No `is_qnx` usage found anywhere** in `content/` BUILD.gn files — QNX support appears minimal.

3. **`content/shell/BUILD.gn`** (lines 1098-1109): Similar `data +=` patterns but within `group("content_shell_crash_test")` which has unconditional `data = [...]` at line 1073. Not affected.

4. **Pattern inconsistency**: Lines 2139 and 3229 use slightly different platform conditions (2139 lacks `is_fuchsia`). This is intentional based on test requirements but worth noting for maintainability.

## Output File

Findings written to a phase-1 working note for this investigation.