# QNX: security interstitials date/time settings fallback

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `//components/security_interstitials/content:security_interstitial_page`
- file: `components/security_interstitials/content/utils.cc`

## Failure signature

```text
FAILED: obj/components/security_interstitials/content/security_interstitial_page/utils.o
../../components/security_interstitials/content/utils.cc:100:2: error: Unsupported target architecture.
  100 | #error Unsupported target architecture.
      |  ^
```

## Root cause

`LaunchDateAndTimeSettings()` has platform-specific implementations for Android, Linux, Mac, and Windows. Fuchsia and iOS use `NOTIMPLEMENTED_LOG_ONCE()`. QNX reached the final unsupported-target branch when the security interstitial content target was built by the wider `cef` graph.

QNX currently has no platform integration for opening a date/time settings UI from Chromium.

## Fix

Patch: `qnx/chromium/security_interstitials_utils_qnx_notimplemented`

Add `BUILDFLAG(IS_QNX)` to the existing Fuchsia/iOS fallback branch:

```cpp
#elif BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_IOS) || BUILDFLAG(IS_QNX)
  NOTIMPLEMENTED_LOG_ONCE();
```

## Verification

Narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/components/security_interstitials/content/security_interstitial_page/utils.o
```

Result:

```text
[100/100] CXX obj/components/security_interstitials/content/security_interstitial_page/utils.o
```

The clean `ninja -C . cef` reached this blocker at `[30919/60706]` after a successful fresh bootstrap.

## Search hints

```bash
rg -n "Unsupported target architecture|LaunchDateAndTimeSettings|security_interstitials/content/utils.cc" /tmp/*.log docs/qnx/history/build-errors
```
