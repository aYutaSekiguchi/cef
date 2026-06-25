# QNX cefsimple libcef.so undefined references after Views desktop UI enable

- Date: 2026-06-24
- Signature: undefined reference to `vtable for BrowserNativeWidgetFactory`
- Stage: link
- Category: symbol-visibility
- Scope: cefsimple / libcef.so

## Symptoms

`cefsimple` link fails at the final executable link step after `libcef.so`
successfully links. `ned parse` reports a single `failed_edges` cluster for
`cefsimple` and 10 `link_undefined_symbol` clusters:

1. `vtable for BrowserNativeWidgetFactory`
2. `base::GetProcessExecutablePath(int)`
3. `HandleOnPerformingDrop(content::WebContents*, content::DropData, ...)`
4. `GetProfilesINI()`
5. `storage_monitor::StorageMonitor::CreateInternal()`
6. `AddContextMenuParamsPropertiesFromPreferences(content::WebContents*, ...)`
7. `media::VideoCaptureGpuChannelHost::GetSharedImageInterface()`
8. `media::VideoCaptureGpuChannelHost::GetInstance()`
9. `blink::LayoutTheme::NativeTheme()`
10. `HatsNextWebDialog::HatsNextWebDialog(...)`

## Root cause

Most QNX stub files existed under
`cef/patch/qnx/chromium/new_files/...` but were either:
- Not wired into the build graph (e.g. `browser_native_widget_factory_qnx.cc`,
  `video_capture_gpu_channel_host_qnx.cc` missing from BUILD.gn)
- Wired inside platform blocks that do not evaluate on QNX (e.g.
  `is_linux`, `is_android`, `is_chromeos` only)
- Mis-sourced or had missing vtable-emitting member definitions
  (`VideoCaptureGpuChannelHost` ctor/dtor/observer methods, etc.)

The QNX port's `is_linux` is true at GN level, so the `is_linux` block evaluates
on QNX and the `is_qnx` block also evaluates, causing duplicate symbol
definitions when both branches add the same source file (e.g.
`process_handle_linux_qnx.cc` was inside the `is_linux` block but the
`is_qnx` block already provided the same symbol).

`HandleOnPerformingDrop` stub had signature mismatch
(`const content::DropData&` vs header's `content::DropData`).

## Fix pattern

- Add missing QNX stub files to the relevant `sources` lists in BUILD.gn.
- Move QNX-only source additions into top-level `if (is_qnx)` blocks
  (never inside `if (is_linux)` or `if (is_android)`).
- When reusing Linux/Android mojom-driven headers in a QNX stub, inherit
  the deps from the existing block (`enable_gpu_channel_media_capture`)
  rather than creating a separate QNX branch.
- Stub signatures must exactly match header declarations; include the
  full `base/functional/callback.h` template, not just the forward decl.
- Guard callers for features QNX does not implement (e.g. `HatsNextWebDialog`).

## Applied change

Modified existing CEF-managed patches:

- `base_BUILD_stubs_is_qnx.patch`: added `process_handle_linux_qnx.cc` to the
  `if (is_qnx)` block (was incorrectly inside `(is_linux && !is_qnx)` block).
- `chrome_browser_ui_tab_contents_BUILD_stubs_is_qnx.patch`: widened the
  `impl` source_set's `if (is_win || is_mac || is_linux || is_chromeos)` to
  `|| is_qnx` so `menu_helper.cc` and `view_handle_drop.cc` compile on QNX.
- `chrome_browser_ui_views_stubs_is_qnx.patch`: added
  `browser_native_widget_factory_qnx.cc` to the existing `is_qnx` block.
- `components_storage_monitor_BUILD_stubs_is_qnx.patch`: moved
  `storage_monitor_linux_qnx.cc` out of the `else if (is_linux)` block
  into a standalone `if (is_qnx)` block.
- `third_party_blink_layout_stubs_qnx.patch`: moved `layout_theme_qnx.cc`
  out of the `if (is_android)` block into a standalone `if (is_qnx)` block.

New CEF-managed patches:

- `media_capture_BUILD_stubs_is_qnx.patch`: added
  `video_capture_gpu_channel_host_qnx.cc` in a standalone `if (is_qnx)` block
  (with `//base`, `//gpu/command_buffer/client`, and `//gpu/ipc/common` deps)
  so the stub is always linked on QNX and the mojom-forward dependency is
  available when compiling the header.
- `chrome_browser_ui_views_frame_browser_view_qnx.patch`: wrapped the
  `HatsNextWebDialog` constructor call in `BrowserView::ShowHatsDialog`
  with `#if !BUILDFLAG(IS_QNX)`.
- `chrome_common_importer_BUILD_qnx.patch`: widened
  `if (is_chromeos || is_linux)` to `|| is_qnx` so `firefox_importer_utils_linux.cc`
  provides `GetProfilesINI` on QNX.

Updated `patch/patch.cfg` to register the three new patches.

Updated QNX new files (tracked via `cef/patch/qnx/chromium/new_files/`):

- `chrome/browser/ui/views/frame/browser_native_widget_factory_qnx.cc`:
  dropped duplicate `CreateBrowserNativeWidget` definition (already in
  `browser_native_widget_factory.cc`).
- `chrome/browser/ui/tab_contents/chrome_web_contents_view_handle_drop_qnx.cc`:
  made empty (real implementation now compiled via the widened
  `is_qnx` block).
- `media/capture/video/video_capture_gpu_channel_host_qnx.cc`: added
  `GetInstance`/`GetSharedImageInterface` plus ctor/dtor/`OnContextLost`/
  `AddObserver`/`RemoveObserver` definitions so the remaining link symbols
  and vtable are emitted in the stub translation unit. `GetInstance` uses
  `base::NoDestructor` to avoid `-Wexit-time-destructors`.

Also updated `VERSION.stamp` from `chromium-147.0.7727.138` to
`chromium-147.0.7727.147` to match `CHROMIUM_BUILD_COMPATIBILITY.txt`.
Note: `VERSION.stamp` is regenerated by the CEF translator tool.

## Verification

```bash
# Clean-tree recipe per qnx-cef-build skill
cd chromium/src
git reset --hard HEAD
gclient sync -f -R
cd third_party/epoll/src && git checkout -f
cd ../farmhash_qnx/src && git checkout -f
cd ../cpuinfo_qnx/src && git checkout -f
cd ../../cef && bash tools/qnx_sync_sources.sh
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root $QNX_SDP_ROOT
# -> Success! QNX CEF project files created. 0 patches failed.
```

User confirmed `cefsimple` link no longer reports the 10 undefined symbols.

## Files touched

Patches:

- `patch/patch.cfg`
- `patch/patches/qnx/chromium/base_BUILD_stubs_is_qnx.patch`
- `patch/patches/qnx/chromium/chrome_browser_ui_views_stubs_is_qnx.patch`
- `patch/patches/qnx/chromium/chrome_browser_ui_tab_contents_BUILD_stubs_is_qnx.patch`
- `patch/patches/qnx/chromium/components_storage_monitor_BUILD_stubs_is_qnx.patch`
- `patch/patches/qnx/chromium/third_party_blink_layout_stubs_qnx.patch`
- `patch/patches/qnx/chromium/media_capture_BUILD_stubs_is_qnx.patch`
- `patch/patches/qnx/chromium/chrome_browser_ui_views_frame_browser_view_qnx.patch`
- `patch/patches/qnx/chromium/chrome_common_importer_BUILD_qnx.patch`

New files:

- `patch/qnx/chromium/new_files/chrome/browser/ui/views/frame/browser_native_widget_factory_qnx.cc`
- `patch/qnx/chromium/new_files/chrome/browser/ui/tab_contents/chrome_web_contents_view_handle_drop_qnx.cc`
- `patch/qnx/chromium/new_files/media/capture/video/video_capture_gpu_channel_host_qnx.cc`

Misc:

- `VERSION.stamp`

## Related notes

- `docs/qnx/history/build-errors/link/qnx-cefsimple-undef-symbol-classification.md`
- `docs/qnx/history/build-errors/link/qnx-cefsimple-non-firefox-imports-resolved.md`
- `docs/qnx/history/build-errors/link/qnx-link-error-classification.md`
- `.agents/skills/qnx-cef-build/SKILL.md` (clean-tree recipe)
- `.agents/skills/build-breakage-loop/SKILL.md` (verification workflow)