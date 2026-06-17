# QNX: TabHelpers framebust helper include guard

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `obj/chrome/browser/ui/ui/tab_helpers.o`

## Failure signature

```text
../../chrome/browser/ui/tab_helpers.cc:675:3: error: use of undeclared identifier 'FramebustBlockTabHelper'
  675 |   FramebustBlockTabHelper::CreateForWebContents(web_contents);
      |   ^
1 error generated.
```

## Root cause

`TabHelpers::AttachTabHelpers()` creates `FramebustBlockTabHelper` in the
non-Android path. QNX is non-Android and the `chrome/browser/ui/blocked_content`
GN target already allows/builds QNX.

However, `tab_helpers.cc` included
`chrome/browser/ui/blocked_content/framebust_block_tab_helper.h` only for
Windows, macOS, Linux, and ChromeOS. QNX therefore compiled the call site
without the declaration.

## Fix

Extend the include guard around the blocked-content framebust helper include to
include `BUILDFLAG(IS_QNX)`.

## Verification

```bash
cd /home/yuta/chromium/test/src
ninja -C out/qnx_release obj/chrome/browser/ui/ui/tab_helpers.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "FramebustBlockTabHelper|tab_helpers|framebust_block_tab_helper" docs/qnx/history/build-errors/compile
```
