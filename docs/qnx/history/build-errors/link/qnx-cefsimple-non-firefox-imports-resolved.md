# QNX cefsimple non-Firefox-import undefined symbols resolved

## Goal
Resolve all QNX `cefsimple` undefined reference errors except Firefox import
(`GetProfilesINI`) which is explicitly not needed.

## Result
- **Start**: 49 distinct undefined refs (after Category 3 disable)
- **End**: 1 undefined ref remaining (`GetProfilesINI` - Firefox import, explicitly excluded)

## Root fixes applied

### 1. BUILD.gn `is_qnx` block additions
- `base/BUILD.gn`: Added `is_qnx` blocks for `platform_thread_linux_qnx.cc`,
  `process_handle_linux_qnx.cc`, `mime_util_xdg_qnx.cc`, `thread_delegate_posix_qnx.cc`,
  `drive_info_posix_qnx.cc`
- `chrome/browser/BUILD.gn`: Added `is_qnx` blocks for stubs and included
  `memory_details_linux.cc` for QNX
- `chrome/browser/ui/BUILD.gn`: Added `is_qnx` blocks for stubs and included
  `browser_native_widget_factory_qnx.cc`
- `chrome/browser/ui/tab_contents/BUILD.gn`: Added `is_qnx` to the block
  including `chrome_web_contents_menu_helper.cc` and
  `chrome_web_contents_view_handle_drop.cc`
- `chrome/browser/ui/views/frame/BUILD.gn`: Added `browser_native_widget_factory_qnx.cc`
- `components/policy/core/browser/BUILD.gn`: Moved `is_qnx` block to include
  `battery_saver_mode_policy_handler_qnx.cc`
- `components/storage_monitor/BUILD.gn`: Moved `is_qnx` block out of
  `else if (is_linux)` to be a separate block
- `media/capture/BUILD.gn`: Added `video_capture_gpu_channel_host.cc` to the
  `is_qnx` block
- `third_party/blink/renderer/core/layout/build.gni`: Added `is_qnx` block
  for `layout_theme_qnx.cc`
- `ui/views/controls/webview/BUILD.gn`: Added `is_qnx` block for
  `unhandled_keyboard_event_handler_qnx.cc`

### 2. QNX stub files created
- `base/files/drive_info_posix_qnx.cc`
- `base/nix/mime_util_xdg_qnx.cc`
- `base/process/process_handle_linux_qnx.cc`
- `base/profiler/thread_delegate_posix_qnx.cc`
- `base/threading/platform_thread_linux_qnx.cc`
- `chrome/browser/memory_details_linux.cc` (reused for QNX)
- `chrome/browser/notifications/notification_platform_bridge_linux_qnx.cc`
- `chrome/browser/policy/battery_saver_mode_policy_handler_qnx.cc`
- `chrome/browser/policy/policy_path_parser_qnx.cc`
- `chrome/browser/ui/linux_ui_qnx.cc` (in chrome/browser/ui/)
- `chrome/browser/ui/status_icons/status_tray_qnx.cc`
- `chrome/browser/ui/tab_contents/chrome_web_contents_view_handle_drop_qnx.cc`
- `chrome/browser/ui/views/chrome_views_delegate_qnx.cc`
- `chrome/browser/ui/views/frame/browser_native_widget_factory_qnx.cc`
- `chrome/browser/ui/views/frame/browser_native_widget_factory.cc` (modified
  to add `Create()` definition for vtable emission)
- `components/storage_monitor/storage_monitor_linux_qnx.cc`
- `media/audio/audio_manager_qnx.cc`
- `media/capture/video/video_capture_gpu_channel_host_qnx.cc` (empty, original
  compiled for QNX)
- `services/resource_coordinator/.../os_metrics_linux_qnx.cc`
- `skia/ext/font_utils_qnx.cc`
- `third_party/blink/renderer/core/layout/layout_theme_qnx.cc`
- `third_party/blink/renderer/platform/fonts/linux/font_cache_linux_qnx.cc`
- `third_party/blink/renderer/platform/fonts/linux/font_cache_linux.cc`
  (modified to add QNX stubs for SystemFontFamily and PlatformFallbackFontForCharacter)
- `ui/linux/linux_ui_qnx.cc`
- `ui/shell_dialogs/shell_dialog_linux_qnx.cc`
- `ui/views/controls/webview/unhandled_keyboard_event_handler_qnx.cc`
- `ui/views/widget/desktop_aura/desktop_window_tree_host_linux_qnx.cc`

### 3. Caller guards
- `chrome/browser/ui/views/frame/browser_view.cc`: Added `#if !BUILDFLAG(IS_QNX)`
  guard around `HatsNextWebDialog` call in `ShowHatsDialog()`

### 4. Patch fixes
- Removed redundant `chrome_browser_render_view_context_menu_isolated_web_app_qnx.patch`
  (already excluded via `chrome_browser_linux_is_qnx.patch`)

## Excluded symbols
- `GetProfilesINI()` - Firefox import (explicitly excluded per goal)
- `HatsNextWebDialog::HatsNextWebDialog()` - resolved by caller guard
