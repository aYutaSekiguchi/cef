# Swapping Chromium's WebRTC to qnx-ports/webrtc was evaluated and rejected

- Date: 2026-06-05
- Signature: qnx-ports/webrtc diverged too far from Chromium M147 despite sharing an older common ancestor
- Stage: compile
- Category: build-graph
- Scope: WebRTC source-of-truth decision

## Symptoms

- A QNX-maintained WebRTC fork existed and looked tempting as a shortcut for QNX support.

## Root cause

- The fork was based around an older milestone line and no longer matched the WebRTC APIs expected by Chromium M147.
- Swapping the submodule would have broken many call sites outside `third_party/webrtc`.

## Fix pattern

- Prefer small downstream patches on the current upstream milestone when a platform fork has drifted too far and the product goal only needs a subset of functionality.

## Applied change

- Reverted the temporary qnx-ports/webrtc submodule experiment.
- Re-enabled the narrower local WebRTC patches and continued with per-blocker fixes on top of the Chromium-owned WebRTC revision.

## Verification

- The build resumed from the official WebRTC source and continued to the next concrete blocker after reapplying the narrow QNX patches.

## Files touched

- `.gitmodules`
- `third_party/webrtc`
- `cef/patch/patch.cfg`
- `cef/patch/patches/qnx/chromium/webrtc_qnx_platform_thread_names.patch`
- `cef/patch/patches/qnx/chromium/webrtc_qnx_byte_order_endian.patch`

## Related notes

- `docs/qnx/build-error-index.md`
