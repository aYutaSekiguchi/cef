# QNX is_linux=false experiment — feature flags and libxml include

- Stage: compile / build-graph
- Branch: `qnx_is_linux_false_experiment`
- Date: 2026-06-11
- Commit: `1a2a14d7d` "QNX experiment: enable desktop feature flags and libxml linux include"
- Patches (4): see commit diff

## What this commit does

With `is_linux=false`, the desktop-only feature flags that used to
be implicitly enabled through `is_linux=true` are now false. This
commit restores the previous Linux-like behavior at the feature-flag
level.

## Patches in this commit

1. `screen_ai_features_qnx_service` —
   `enable_screen_ai_service` in
   `services/screen_ai/buildflags/features.gni:9` was
   `is_linux || is_mac || is_chromeos || is_win`. QNX reuses the
   Linux bucket.
2. `speech_service_qnx_feature` —
   `enable_speech_service` in
   `chrome/services/speech/buildflags/buildflags.gni:11` was
   `is_chromeos || is_linux || is_mac || is_win`.
3. `chrome_root_store_qnx_feature` —
   `chrome_root_store_cert_management_ui` in
   `chrome/common/features.gni:39` was
   `is_win || is_mac || is_linux || is_chromeos`.
4. `libxml_qnx_linux_include` — libxml picks the OS-specific
   generated header set from `os_include = "linux"` when
   `is_linux=true`. On QNX (with `is_linux=false`), the build was
   failing with `Undefined identifier in string expansion: os_include`.
   We add `is_qnx` to the `if (is_linux || is_chromeos || is_android
   || is_fuchsia)` branch in `third_party/libxml/BUILD.gn:7` so the
   QNX build reuses the existing Linux include bucket rather than
   introducing a parallel tree.

## Browsertest variants

`enable_screen_ai_browsertests` and other test-time-only flags are
intentionally left off; we mirror the previous `is_linux=true`
behavior but stay conservative for test-time-only platforms where
the underlying library is not actually shipped.

## What is NOT yet done (deferred)

- Dawn/ANGLE/breakpad `is_linux`-gated test targets that fall out
  of the graph when `is_linux=false` are still pending decision.
  See the open question about whether to add `is_qnx` to those
  targets or to gate them out of the graph entirely.
