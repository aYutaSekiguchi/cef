# QNX: web_app_dialogs.h platform assert

## Stage

- stage: compile
- category: build-graph / static-assert
- file: `chrome/browser/ui/web_applications/web_app_dialogs.h`
- downstream: `chrome/browser/extensions/api/passwords_private/passwords_private_delegate_impl.cc`

## Failure signature

```text
In file included from .../passwords_private_delegate_impl.cc:51:
In file included from .../web_app_dialog_utils.h:9:
../../chrome/browser/ui/web_applications/web_app_dialogs.h:27:15: error: static assertion failed due to requirement '((0)) || ((0)) || ((0)) || ((0))'
   27 | static_assert(BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
      |               ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   28 |               BUILDFLAG(IS_CHROMEOS));
      |               ~~~~~~~~~~~~~~~~~~~~~~
../../build/buildflag.h:45:25: note: expanded from macro 'BUILDFLAG'
1 error generated.
```

## Root cause

`web_app_dialogs.h` declares desktop-only dialog helpers and uses a `static_assert` to keep the file out of mobile/embedded builds. The existing assertion was `IS_WIN || IS_MAC || IS_LINUX || IS_CHROMEOS`; QNX was added to the platform-assert allowlist by `chrome_browser_ui_asserts_allow_qnx` only for `OR`-style guards, not for `static_assert` expressions. Several QNX CEF paths (e.g. `passwords_private_delegate_impl.cc`) include this header through `web_app_dialog_utils.h`, so the assertion fires before any other check.

## Fix

Extend the static assert to allow QNX:

```cpp
static_assert(BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) ||
              BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_QNX));
```

This mirrors the rationale used by `web_app_install_info_qnx` for the same family of headers.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/extensions/extensions/passwords_private_delegate_impl.o
```

Result: `passwords_private_delegate_impl.o` compiles successfully.

## Search hints

```bash
rg -n "web_app_dialogs|web_app_dialog_utils|passwords_private_delegate_impl|IS_CHROMEOS" docs/qnx/history/build-errors/compile
```
