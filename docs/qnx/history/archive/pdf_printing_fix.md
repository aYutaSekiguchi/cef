# pdf/BUILD.gn printing Dependency QNX Exclusion Fix

## Fix Summary

Added QNX exclusion in the following 4 locations:

| Line | target | Fix |
|------|--------|-----|
| 236 | `source_set("internal")` | `deps -= [ "//printing" ]` + `public_deps -= [ "//printing/mojom" ]` |
| 451 | `static_library("pdf_view_web_plugin")` | `deps -= [ "//printing" ]` |
| 499 | `source_set("pdf_test_utils")` | `deps -= [ "//printing" ]` ← **Newly added** |
| 590 | `test("pdf_unittests")` | `deps -= [ "//printing" ]` ← **Newly added** |

## Fix for pdf_test_utils

```gn
    deps = [
      ":accessibility",
      ":buildflags",
      ...
      "//printing",
      ...
    ]

    if (is_qnx) {
      deps -= [ "//printing" ]
    }
  }
```

## Fix for pdf_unittests

```gn
    deps = [
      ...
      "//printing",
      ...
    ]

    if (is_qnx) {
      deps -= [ "//printing" ]
    }

    data_deps = [
```

## Verification Results

**gn gen result**: Failed with pkg-config error (gbm library not found)
- This error is unrelated to printing dependency
- No printing-related errors when `enable_printing=false`

**Current error**:
```
subprocess.CalledProcessError: Command '['pkg-config', '--variable', 'prefix', 'gbm']`
```
→ UI/gfx related (minigbm) error. Separate issue from printing dependency.

## Conclusion

QNX exclusion for pdf/BUILD.gn printing dependencies is complete.
The overall QNX build error is a pkg-config (gbm library) configuration issue,
a separate problem outside the scope of this fix.