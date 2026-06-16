# QNX: sharing_hub desktop controller override guard

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `//chrome/browser/ui/sharing_hub:impl`
- file: `chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller_desktop_impl.h`

## Failure signature

```text
../../chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller_desktop_impl.h:57:56: error: only virtual member functions can be marked 'override'
  std::vector<SharingHubAction> GetFirstPartyActions() override;
                                                       ^~~~~~~~
../../chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller_desktop_impl.h:58:58: error: only virtual member functions can be marked 'override'
  base::WeakPtr<SharingHubBubbleController> GetWeakPtr() override;
                                                         ^~~~~~~~
../../chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller_desktop_impl.h:59:57: error: only virtual member functions can be marked 'override'
  void OnActionSelected(const SharingHubAction& action) override;
                                                        ^~~~~~~~
../../chrome/browser/ui/sharing_hub/sharing_hub_bubble_controller_desktop_impl.h:60:25: error: only virtual member functions can be marked 'override'
  void OnBubbleClosed() override;
                        ^~~~~~~~
```

## Root cause

`chrome/browser/ui/sharing_hub/BUILD.gn` selects `sharing_hub_bubble_controller_desktop_impl.{h,cc}` for non-ChromeOS platforms. QNX enters that desktop branch once the BUILD.gn assert is extended by `chrome_browser_ui_asserts_allow_qnx.patch`.

However, the base class declarations for these four methods in `sharing_hub_bubble_controller.h` were guarded only by:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
```

On QNX, the base virtual methods were therefore absent while the desktop implementation still declared `override`, producing the compile failure.

## Fix

Patch: `qnx/chromium/sharing_hub_bubble_controller_desktop_impl_qnx_is_linux_fallback`

Extend the same desktop platform set to include QNX in all related locations:

- base virtual declarations in `sharing_hub_bubble_controller.h`
- desktop override declarations in `sharing_hub_bubble_controller_desktop_impl.h`
- matching method definitions in `sharing_hub_bubble_controller_desktop_impl.cc`

This follows the existing QNX port pattern of merging QNX into the Linux-like desktop branch rather than adding a new behavior branch.

## Verification

After a full clean tree bootstrap, the narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/ui/sharing_hub/impl/sharing_hub_bubble_controller_desktop_impl.o
```

Result:

```text
[100/100] CXX obj/chrome/browser/ui/sharing_hub/impl/sharing_hub_bubble_controller_desktop_impl.o
```

The wider `ninja -C . cef` then progressed past sharing_hub and exposed the next blocker in `chrome/browser/web_applications/web_app.cc`.

## Search hints

```bash
rg -n "only virtual member functions can be marked 'override'|GetFirstPartyActions\(\)|OnBubbleClosed\(\)|sharing_hub_bubble_controller_desktop_impl" /tmp/*.log docs/qnx/history/build-errors
```
