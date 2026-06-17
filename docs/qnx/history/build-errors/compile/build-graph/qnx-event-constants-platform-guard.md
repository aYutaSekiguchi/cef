# QNX: feature_engagement event constants platform guard

## Stage

- stage: compile
- category: build-graph / missing-constant
- file: `components/feature_engagement/public/event_constants.h`
- downstream: `chrome/browser/ui/views/user_education/browser_user_education_service.cc`

## Failure signature

```text
../../chrome/browser/ui/views/user_education/browser_user_education_service.cc:805:47: error: no member named 'kGlicOnboardingCompleted' in namespace 'feature_engagement::events'
  805 |                   feature_engagement::events::kGlicOnboardingCompleted,
../../chrome/browser/ui/views/user_education/browser_user_education_service.cc:820:47: error: no member named 'kGlicOnboardingCompleted' in namespace 'feature_engagement::events'
../../chrome/browser/ui/views/user_education/browser_user_education_service.cc:1127:47: error: no member named 'kSplitViewCreated' in namespace 'feature_engagement::events'
3 errors generated.
```

## Root cause

`components/feature_engagement/public/event_constants.h` declares desktop
event constants under:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)
```

The companion `event_constants.cc` defines them under the same guard.
QNX reaches `browser_user_education_service.cc` (the desktop IPH
service), which references the constants without a guard. QNX is not
in the platform expression, so the references fail to resolve.

`feature_constants_qnx.patch` already covers the feature constants
header; the event constants header is its sibling and needs the same
treatment.

## Fix

Extend the guard to allow QNX:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_QNX)
```

Both the header and the companion .cc share the same guard, so a single
header change is sufficient for the desktop IPH service to compile.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/ui/ui/browser_user_education_service.o
```

Result: `browser_user_education_service.o` compiles successfully.

## Search hints

```bash
rg -n "kGlicOnboardingCompleted|kSplitViewCreated|event_constants|feature_engagement::events" docs/qnx/history/build-errors/compile
```
