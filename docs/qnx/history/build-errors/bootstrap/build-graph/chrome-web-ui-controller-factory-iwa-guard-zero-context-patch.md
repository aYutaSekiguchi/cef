# Chrome WebUI IWA guard patch needs contextual replacement

- Date: 2026-07-28
- Signature: `chrome/browser/ui/webui/chrome_web_ui_controller_factory.cc:179:6: error: unused function [-Werror,-Wunused-function]`
- Stage: bootstrap
- Category: build-graph
- Scope: `chrome_web_ui_controller_factory_iwa_guard_qnx.patch`

## Root cause

The patch used a one-line replacement with no unchanged context. During clean
replay it was not anchored, leaving the helper under `!IS_ANDROID` while its
caller remained guarded for Linux/ChromeOS. QNX therefore compiled an unused
helper and stopped under `-Werror`.

## Fix and verification

The managed patch was regenerated as a contextual replacement around the
function declaration. No pragma or warning suppression was added. The
previously failing `chrome_web_ui_controller_factory.o` completed with `-j10`.

## Files touched

- `cef/patch/patches/qnx/chromium/chrome_web_ui_controller_factory_iwa_guard_qnx.patch`

