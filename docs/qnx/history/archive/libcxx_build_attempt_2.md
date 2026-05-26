# libc++ Build Attempt 2 - QNX Target

## Build Command
```bash
export QNX_HOST=<QNX_SDP_ROOT>/host/linux/x86_64
export QNX_TARGET=<QNX_SDP_ROOT>/target/qnx
cd <CHROMIUM_SRC_ROOT>
ninja -C out/qnx_x64 libc++
```

## Result: FAILED

## Errors Found

### 1. Locale Support - Rune Table Missing
```
error: unknown rune table for this platform -- do you mean to define _LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE?
```
- File: `third_party/libc++/src/include/__locale:439`
- QNX lacks proper locale/rune definitions

### 2. Missing pthread Definitions
```
error: use of undeclared identifier 'PTHREAD_MUTEX_RECURSIVE'
error: use of undeclared identifier 'pthread_cond_timedwait'
error: use of undeclared identifier 'nanosleep'
```
- File: `third_party/libc++/src/include/__thread/support/pthread.h`

### 3. Missing C Library Functions
```
error: no member named 'asprintf' in the global namespace
error: no member named 'wcsnrtombs' in the global namespace
error: no member named 'mbsnrtowcs' in the global namespace
```
- File: `third_party/libc++/src/include/__locale_dir/support/qnx.h:42,61,67`
- QNX doesn't provide these functions

### 4. Function Signature Mismatches
```
error: too many arguments to function call, expected 4, have 5
```
- `wcsnrtombs` and `mbsnrtowcs` have different signatures in QNX's wchar.h

## Root Cause Analysis

The libc++ QNX port is incomplete:
1. There's a `qnx.h` locale support file but it assumes functions that don't exist in QNX 8.0
2. The pthread support file (`pthread.h`) doesn't account for QNX's pthread implementation differences
3. No rune table is defined for QNX

## Required Fixes

1. **Add missing function wrappers** in `qnx.h`:
   - Implement `asprintf` as a wrapper using `vasprintf`
   - Implement `wcsnrtombs` using `wcsrtombs` + length limit
   - Implement `mbsnrtowcs` using `mbsrtowcs` + length limit

2. **Define `_LIBCPP_PROVIDES_DEFAULT_RUNE_TABLE`** or add QNX-specific rune table

3. **Fix pthread support** - QNX may use different pthread constants

4. **Check pthread.h availability** - ensure proper includes for `nanosleep`, etc.

## Files Needing Changes

| File | Issue |
|------|-------|
| `third_party/libc++/src/include/__locale_dir/support/qnx.h` | Missing function implementations |
| `third_party/libc++/src/include/__thread/support/pthread.h` | QNX pthread differences |
| Build configuration | Need to add rune table definition |

## Estimated Effort

**High** - The QNX libc++ support needs significant work. A complete port requires:
- Implementing wrapper functions for missing C library APIs
- Adding QNX-specific thread support
- Defining locale/rune tables
- Testing all libc++ components
