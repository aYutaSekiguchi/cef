# QNX libcef.so whole-archive duplicate source objects

## Failure signature

Stage: link
Category: duplicate-symbols
Target: `libcef.so` / `cefsimple`

Representative diagnostics after the ICU duplicate-symbol fix:

```text
chrome/browser/enterprise/watermark/settings.cc:61: multiple definition of `enterprise_watermark::PercentageToSkAlpha(int)'; obj/chrome/browser/enterprise/watermark/watermark_view_lib/settings.o:first defined here
components/enterprise/browser/promotion/promotion_eligibility_checker.cc:29: multiple definition of `enterprise_promotion::PromotionEligibilityChecker::PromotionEligibilityChecker(...)'; obj/components/enterprise/browser/promotion/promotion/promotion_eligibility_checker.o:first defined here
base/metrics/field_trial_params.h:118: multiple definition of `RegisterReadAnythingProfilePrefs(user_prefs::PrefRegistrySyncable*)'; obj/chrome/browser/ui/read_anything/read_anything/read_anything_prefs.o:first defined here
chrome/browser/ui/frame/window_frame_util.cc:13: multiple definition of `WindowFrameUtil::CalculateWindowsCaptionButtonBackgroundAlpha(unsigned char)'; obj/chrome/browser/ui/frame/frame/window_frame_util.o:first defined here
```

## Root cause

QNX's `libcef.so` link wraps rspfile inputs in `-Wl,--whole-archive`. Static libraries are therefore fully expanded. If the same `.cc` is compiled into both an aggregate Chromium library and a smaller component/source_set target, the final `libcef.so` link sees duplicate object definitions.

The duplicate groups were:

| Source | Aggregate object | Component/source_set object |
| --- | --- | --- |
| `chrome/browser/enterprise/watermark/settings.cc` | `obj/chrome/browser/browser/settings.o` | `obj/chrome/browser/enterprise/watermark/watermark_view_lib/settings.o` |
| `components/enterprise/browser/promotion/promotion_eligibility_checker.cc` | `obj/components/enterprise/enterprise/promotion_eligibility_checker.o` | `obj/components/enterprise/browser/promotion/promotion/promotion_eligibility_checker.o` |
| `chrome/browser/ui/read_anything/read_anything_prefs.cc` | `obj/chrome/browser/ui/ui/read_anything_prefs.o` | `obj/chrome/browser/ui/read_anything/read_anything/read_anything_prefs.o` |
| `chrome/browser/ui/frame/window_frame_util.cc` | `obj/chrome/browser/ui/ui/window_frame_util.o` | `obj/chrome/browser/ui/frame/frame/window_frame_util.o` |

## Fix

Patch: `cef/patch/patches/qnx/chromium/libcef_whole_archive_duplicate_sources_qnx.patch`

Changes:

- `chrome/browser/BUILD.gn`
  - On QNX, do not compile `enterprise/watermark/settings.{cc,h}` directly into `//chrome/browser:browser`; keep `//chrome/browser/enterprise/watermark:watermark_view_lib`.
- `components/enterprise/BUILD.gn`
  - On QNX, do not compile `browser/promotion/promotion_eligibility_checker.{cc,h}` into `//components/enterprise:enterprise`; keep `//components/enterprise/browser/promotion`.
- `chrome/browser/ui/BUILD.gn`
  - On QNX, remove `read_anything/read_anything_prefs.{cc,h}` from the aggregate `//chrome/browser/ui:ui`; keep `//chrome/browser/ui/read_anything`.
  - On QNX, do not compile `frame/window_frame_util.{cc,h}` directly into the aggregate `//chrome/browser/ui:ui`; keep `//chrome/browser/ui/frame`.

## Verification

```text
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
ninja -C out/qnx_release libcef.so
EXIT:0
```

The successful link also confirmed that the prior ICU duplicate-symbol group was gone after `task_manager_no_hidden_icu_qnx.patch`:

```text
hidden icui18n 0
hidden icuuc 0
normal icui18n 1
normal icuuc 1
```

## Search hints

```bash
rg -n "enterprise_watermark|watermark_view_lib|promotion_eligibility_checker|read_anything_prefs|window_frame_util|whole-archive|multiple definition" docs/qnx/history/build-errors
```
