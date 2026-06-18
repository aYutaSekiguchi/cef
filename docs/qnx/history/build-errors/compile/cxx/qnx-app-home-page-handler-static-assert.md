# QNX: App Home page handler platform assertion

## Stage

- stage: compile
- category: cxx / static_assert platform guard
- target: `obj/chrome/browser/ui/webui/app_home/impl/app_home_ui.o`

## Failure signature

```text
In file included from ../../chrome/browser/ui/webui/app_home/app_home_ui.cc:9:
../../chrome/browser/ui/webui/app_home/app_home_page_handler.h:26:15: error: static assertion failed due to requirement '((0)) || ((0)) || ((0))'
   26 | static_assert(BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX));
```

## Root cause

QNX includes the desktop App Home WebUI deps, so `app_home_ui.cc` and the
`app_home_page_handler` implementation are compiled. The page handler had
explicit desktop-only assertions in both the header and implementation, but the
allowed set was limited to Win/Mac/Linux and did not include QNX.

## Fix

Extend the `static_assert` platform set in both files to include
`BUILDFLAG(IS_QNX)`:

- `chrome/browser/ui/webui/app_home/app_home_page_handler.h`
- `chrome/browser/ui/webui/app_home/app_home_page_handler.cc`

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/ui/webui/app_home/impl/app_home_ui.o
```

Result: `EXIT:0`; no C++ errors emitted.

## Search hints

```bash
rg -n "app_home_page_handler|app_home_ui|App Home" docs/qnx/history/build-errors
```
