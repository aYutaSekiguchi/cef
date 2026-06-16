# QNX: memory_details ZygoteHost usage must follow USE_ZYGOTE

## Stage

- stage: compile
- category: build-graph
- file: `chrome/browser/memory_details.cc`

## Failure signature

```text
../../chrome/browser/memory_details.cc:338:18: error: no member named 'ZygoteHost' in namespace 'content'
  338 |     if (content::ZygoteHost::GetInstance()->IsZygotePid(process.pid)) {
      |         ~~~~~~~~~^
1 error generated.
```

## Root cause

QNX disables `USE_ZYGOTE` via `zygote_features_disable_qnx.patch` because the port does not use Chromium's Linux zygote/sandbox stack. `memory_details.cc` correctly includes `zygote_host_linux.h` only when `BUILDFLAG(USE_ZYGOTE)` is true, but its `ZygoteHost` usage was still guarded by a broader POSIX expression:

```cpp
#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_ANDROID)
```

For QNX this allowed the use site to compile while the declaration was absent.

## Fix

Match the use site to the include guard:

```cpp
#if BUILDFLAG(USE_ZYGOTE)
```

## Verification

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/browser/memory_details.o
```

Result: `memory_details.o` compiles successfully.

## Search hints

```bash
rg -n "memory_details|ZygoteHost|IsZygotePid|USE_ZYGOTE" docs/qnx/history/build-errors/compile
```
