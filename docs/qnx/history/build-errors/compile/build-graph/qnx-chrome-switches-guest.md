# QNX: chrome_switches kGuest for lifetime switch_utils

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `obj/chrome/browser/browser/switch_utils.o`

## Failure signature

```text
../../chrome/browser/lifetime/switch_utils.cc:31:15: error: no member named 'kGuest' in namespace 'switches'
    switches::kGuest,
    ~~~~~~~~~~^
../../chrome/browser/lifetime/switch_utils.cc:41:39: error: cannot use incomplete type 'const char *const[]' as a range
  for (const char* switch_to_remove : kSwitchesToRemoveOnAutorestart) {
                                      ^
```

## Root cause

`chrome/browser/lifetime/switch_utils.cc` unconditionally removes
`switches::kGuest` from the autorestart switch list. The declaration and
definition in `chrome/common/chrome_switches.{h,cc}` were guarded for Linux,
ChromeOS, macOS, and Windows, but not QNX.

## Fix

Extend the desktop guard around `kGuest` (and the adjacent
`kEnableNewAppMenuIcon`) to include `BUILDFLAG(IS_QNX)`.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/browser/switch_utils.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "kGuest|switch_utils|chrome_switches" docs/qnx/history/build-errors/compile
```
