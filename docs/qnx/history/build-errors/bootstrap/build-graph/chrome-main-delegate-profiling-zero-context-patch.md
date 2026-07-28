# Chrome main delegate profiling guard needs contextual replacements

- Date: 2026-07-28
- Signature: `chrome/app/chrome_main_delegate.cc:409:6: error: unused function [-Werror,-Wunused-function]`
- Stage: bootstrap
- Category: build-graph
- Scope: `chrome_main_delegate_profiling_shutdown_guard_qnx.patch`

## Root cause

The two guard replacements were context-less hunks. Clean replay left the
helper under the generic non-Android guard while `ZygoteForked()` remained
Linux/ChromeOS-only, producing an unused function on QNX.

## Fix and verification

The managed patch was reduced to one contextual hunk covering both guard
replacements. No pragma or warning suppression was added. The previously
failing `chrome_main_delegate.o` completed with `-j10`.

## Files touched

- `cef/patch/patches/qnx/chromium/chrome_main_delegate_profiling_shutdown_guard_qnx.patch`

