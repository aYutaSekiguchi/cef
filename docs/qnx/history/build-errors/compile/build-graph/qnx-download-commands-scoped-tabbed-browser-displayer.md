# QNX: download_commands ScopedTabbedBrowserDisplayer include guard

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `obj/chrome/browser/browser/download_commands.o`

## Failure signature

```text
../../chrome/browser/download/download_commands.cc:183:11: error: no member named 'ScopedTabbedBrowserDisplayer' in namespace 'chrome'
../../chrome/browser/download/download_commands.cc:184:10: error: use of undeclared identifier 'browser_displayer'
../../chrome/browser/download/download_commands.cc:185:10: error: use of undeclared identifier 'browser_displayer'
```

## Root cause

`DownloadCommands::GetBrowser()` had already been enabled for QNX by
`download_commands_qnx.patch`, because QNX needs the desktop PDF/download
browser helpers. However, the include block for the desktop browser types still
used only:

```cpp
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_MAC)
```

As a result, QNX compiled the function body but did not include
`chrome/browser/ui/scoped_tabbed_browser_displayer.h`, so the `chrome::` type
was not declared.

## Fix

Extend the include guard in `chrome/browser/download/download_commands.cc` to
include `BUILDFLAG(IS_QNX)`, matching the existing QNX-enabled declaration and
function-body guards in `download_commands_qnx.patch`.

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/browser/download_commands.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "ScopedTabbedBrowserDisplayer|download_commands|browser_displayer" docs/qnx/history/build-errors/compile
```
