# QNX: BrowserFrameViewFactoryViews QNX path

## Stage

- stage: compile
- category: cxx / undeclared-identifier
- target: `obj/chrome/browser/ui/ui/browser_frame_view_factory_views.o`

## Failure signature

```text
../../chrome/browser/ui/views/frame/browser_frame_view_factory_views.cc:123:10: error: use of undeclared identifier 'CreateBrowserFrameViewLinux'; did you mean 'CreateBrowserFrameView'?
  123 |   return CreateBrowserFrameViewLinux(widget, browser_view);
```

## Root cause

`browser_frame_view_factory_views.cc` defines
`CreateBrowserFrameViewLinux` and the helper
`CreateOpaqueBrowserFrameViewLinux` inside an anonymous namespace guarded
by `#if BUILDFLAG(IS_LINUX)`. Those functions include
`ui/linux/linux_ui.h`, `ui/linux/nav_button_provider.h`,
`browser_frame_view_linux_native.h`, `browser_native_widget_aura_linux.h`,
etc., and the underlying `//ui/linux` target asserts on `is_linux`, so
they cannot be compiled on QNX.

The top-level factory dispatcher:

```cpp
std::unique_ptr<BrowserFrameView> CreateBrowserFrameView(
    BrowserWidget* widget, BrowserView* browser_view) {
#if BUILDFLAG(IS_WIN)
  return CreateBrowserFrameViewWin(widget, browser_view);
#else
  return CreateBrowserFrameViewLinux(widget, browser_view);
#endif
}
```

takes the `#else` branch on QNX and calls the missing helper.

## Fix

Add a dedicated QNX path that mirrors the Win non-native-frame branch:
return `PictureInPictureBrowserFrameView` for picture-in-picture
windows and a plain `OpaqueBrowserFrameView` for everything else.
Wire it into the dispatcher with an explicit `BUILDFLAG(IS_QNX)` branch.

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release obj/chrome/browser/ui/ui/browser_frame_view_factory_views.o
```

Result: `EXIT:0`; no errors emitted.

## Search hints

```bash
rg -n "CreateBrowserFrameViewLinux|browser_frame_view_factory_views" docs/qnx/history/build-errors
```
