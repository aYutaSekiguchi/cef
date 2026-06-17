# QNX: signin missing symbols (history sync, supervised signout, profile creation, signin promo)

## Stage

- stage: compile
- category: build-graph / missing-symbol
- downstream: `chrome/browser/ui/signin/signin_view_controller.cc`

## Failure signature

```text
../../chrome/browser/ui/signin/signin_view_controller.cc:486:15: error: no member named 'kProfileCreationFrictionReductionExperimentSkipCustomizeProfile' in namespace 'switches'
../../chrome/browser/ui/signin/signin_view_controller.cc:536:37: error: no member named 'CreateSyncHistoryOptInDelegate' in 'SigninViewControllerDelegate'
../../chrome/browser/ui/signin/signin_view_controller.cc:538:50: error: too many arguments to function call, expected 3, have 4
../../chrome/browser/ui/signin/signin_view_controller.cc:817:28: error: no member named 'kEnableSupervisedUserVersionSignOutDialog' in namespace 'supervised_user'
4 errors generated.
```

## Root cause

`signin_view_controller.cc` (DICE signin history-sync and signout flow) and
`signin_promo.cc` reference desktop-only signin feature symbols unconditionally:

1. `SigninViewControllerDelegate::CreateSyncHistoryOptInDelegate`, a
   static factory declared in `signin_view_controller_delegate.h` and
   defined in `signin_view_controller_delegate_views.cc`. Both
   declarations are guarded by `BUILDFLAG(IS_WIN) ||
   BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)`.
2. `supervised_user::kEnableSupervisedUserVersionSignOutDialog`,
   declared in `components/supervised_user/core/common/features.h` and
   defined in `components/supervised_user/core/common/features.cc`
   under `BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) ||
   BUILDFLAG(IS_WIN)`.
3. `switches::kProfileCreationFrictionReductionExperimentSkipCustomizeProfile`,
   declared in `components/signin/public/base/signin_switches.h` and defined
   in `components/signin/public/base/signin_switches.cc` under
   `BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)`, excluding
   QNX from both declaration visibility and storage.
4. `switches::kSignInPromoMaterialNextUI`, referenced by
   `chrome/browser/signin/signin_promo.cc` and declared/defined under the same
   Win/Mac/Linux-only signin switches guard.

## Fix

Extend each platform guard to include `BUILDFLAG(IS_QNX)` in all four
files:

```cpp
// signin_view_controller_delegate.h / signin_view_controller_delegate_views.cc
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_QNX)
static SigninViewControllerDelegate* CreateSyncHistoryOptInDelegate(...);
#endif

// components/supervised_user/core/common/features.{h,cc}
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_QNX)
BASE_FEATURE(kEnableSupervisedUserVersionSignOutDialog, ...);
#endif

// components/signin/public/base/signin_switches.{h,cc}
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_QNX)
BASE_DECLARE_FEATURE(kProfileCreationFrictionReductionExperimentSkipCustomizeProfile);
BASE_FEATURE(kProfileCreationFrictionReductionExperimentSkipCustomizeProfile, ...);

BASE_DECLARE_FEATURE(kSignInPromoMaterialNextUI);
BASE_FEATURE(kSignInPromoMaterialNextUI, ...);
#endif
```

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/ui/signin/impl/signin_view_controller.o
ninja -C . obj/chrome/browser/browser/signin_promo.o
```

Result: `signin_view_controller.o` compiles successfully.

## Search hints

```bash
rg -n "kProfileCreationFrictionReductionExperimentSkipCustomizeProfile|kSignInPromoMaterialNextUI|CreateSyncHistoryOptInDelegate|kEnableSupervisedUserVersionSignOutDialog|signin_view_controller|signin_promo" docs/qnx/history/build-errors/compile
```
