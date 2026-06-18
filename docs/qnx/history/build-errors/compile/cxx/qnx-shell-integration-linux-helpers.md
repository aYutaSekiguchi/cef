# QNX: shell_integration_linux helper guards

## Stage

- stage: compile
- category: cxx / missing Linux desktop helper declarations
- target: `obj/chrome/browser/browser/shell_integration_linux.o`

## Failure signature

```text
../../chrome/browser/shell_integration_linux.cc:162:26: error: no member named 'GetDesktopName' in namespace 'chrome'
  162 |   argv.push_back(chrome::GetDesktopName(env.get()));

../../chrome/browser/shell_integration_linux.cc:431:16: error: no member named 'GetAppDesktopShortcutFilename' in namespace 'web_app'
  431 |       web_app::GetAppDesktopShortcutFilename(profile_path, app_name)

/home/yuta/qnx800/target/qnx/usr/include/c++/v1/__tree:669:21: error: field has incomplete type 'web_app::DesktopActionInfo'
```

## Root cause

QNX compiles `shell_integration_linux.cc` to reuse Linux desktop integration
helpers. That file depends on helper declarations/types that were still guarded
for Linux only:

- `chrome::GetDesktopName()` in `chrome/common/channel_info.{h,posix.cc}`
- `web_app::DesktopActionInfo` and `GetAppDesktopShortcutFilename()` via
  `web_app_shortcut_linux.h`
- the Linux `web_app_shortcut_linux.cc` implementation in the web applications
  source set
- `OsIntegrationTestOverride::environment()` used by `web_app_shortcut_linux.cc`

QNX only needed the shortcut implementation, not the broader Linux-only web app
OS integration block.

## Fix

- Expose `chrome::GetDesktopName()` for `IS_QNX` in:
  - `chrome/common/channel_info.h`
  - `chrome/common/channel_info_posix.cc`
- Include `web_app_shortcut_linux.h` and enable `ShortcutInfo::actions` for QNX
  in `web_app_shortcut.h`.
- Add only `os_integration/web_app_shortcut_linux.{cc,h}` to QNX web app
  sources.
- Expose `OsIntegrationTestOverride::environment()` for QNX because the Linux
  shortcut implementation references it.

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/browser/shell_integration_linux.o \
  obj/chrome/browser/web_applications/web_applications/web_app_shortcut_linux.o \
  obj/chrome/common/channel_info/channel_info_posix.o
```

Result: `EXIT:0`; no C++ errors emitted.

## Search hints

```bash
rg -n "GetDesktopName|GetAppDesktopShortcutFilename|DesktopActionInfo|shell_integration_linux" docs/qnx/history/build-errors
```
