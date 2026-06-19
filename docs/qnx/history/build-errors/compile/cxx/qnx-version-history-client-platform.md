# QNX version_history_client platform bucket

## Failure signature

Stage: compile
Category: cxx
Target: `obj/chrome/browser/upgrade_detector/impl/version_history_client.o`

Primary diagnostics:

```text
../../chrome/browser/upgrade_detector/version_history_client.cc:198:2: error: Unsupported platform
#error Unsupported platform
 ^
../../chrome/browser/upgrade_detector/version_history_client.cc:204:20: error: expected ')'
      "platforms/" CURRENT_PLATFORM
                   ^
```

## Root cause

`GetVersionReleasesUrl()` selects a VersionHistory API platform bucket at compile time. The supported upstream buckets are Windows, Linux, Mac, and ChromeOS. QNX is not listed, so QNX falls through to `#error Unsupported platform`, and `CURRENT_PLATFORM` remains undefined.

QNX does not have a VersionHistory API platform bucket. The existing QNX port treats many Chrome desktop services as Linux-like when an external API only distinguishes broad desktop platform families.

## Fix

Patch: `cef/patch/patches/qnx/chromium/version_history_client_qnx_linux_platform.patch`

Change:

- Reuse the Linux VersionHistory API platform bucket for QNX:
  - `#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)`
  - `#define CURRENT_PLATFORM "linux"`

## Verification

```text
./out/qnx_release/ninja_qnx.sh obj/chrome/browser/upgrade_detector/impl/version_history_client.o
EXIT:0
```

Patch state check on the patched tree:

```text
patch -p0 --reverse --dry-run < cef/patch/patches/qnx/chromium/version_history_client_qnx_linux_platform.patch
checking file chrome/browser/upgrade_detector/version_history_client.cc
rc=0
```
