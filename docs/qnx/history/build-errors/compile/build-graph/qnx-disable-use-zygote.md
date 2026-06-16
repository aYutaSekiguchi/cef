# QNX: disable USE_ZYGOTE

## Stage

- stage: compile
- category: build-graph / unsupported-platform-feature
- target: `//content/app:content_main_runner_app`
- files:
  - `content/public/common/zygote/features.gni`
  - `content/app/content_main_runner_impl.cc`
  - `content/public/app/content_main_delegate.h`

## Failure signature

```text
../../content/app/content_main_runner_impl.cc:561:13: error: no member named 'ZygoteStarting' in 'content::ContentMainDelegate'
  delegate->ZygoteStarting(&zygote_fork_delegates);
  ~~~~~~~~  ^
../../content/app/content_main_runner_impl.cc:596:13: error: no member named 'ZygoteForked' in 'content::ContentMainDelegate'
  delegate->ZygoteForked();
  ~~~~~~~~  ^
```

## Root cause

`content/public/common/zygote/features.gni` defined:

```gn
use_zygote = is_posix && !is_android && !is_apple
```

QNX is POSIX, so `USE_ZYGOTE=1` was generated for the QNX build. `content_main_runner_impl.cc` then compiled the zygote path and called `ContentMainDelegate::ZygoteStarting()` / `ZygoteForked()`.

However, those hooks are declared only for Linux/ChromeOS:

```cpp
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
virtual void ZygoteStarting(...);
virtual void ZygoteForked() {}
#endif
```

QNX also does not provide Chromium's Linux zygote/sandbox implementation: `content/zygote/BUILD.gn` builds real zygote sources only for `is_linux || is_chromeos`; otherwise `//content/zygote` is an empty group. The Linux zygote files depend on Linux sandbox headers and APIs.

## Fix

Patch: `qnx/chromium/zygote_features_disable_qnx`

Exclude QNX from `use_zygote`:

```gn
use_zygote = is_posix && !is_android && !is_apple && !is_qnx
```

This matches the current QNX runtime expectation of using the no-zygote path rather than trying to port Linux zygote/sandbox internals.

## Verification

After `gn gen`, the narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
gn gen .
ninja -C . obj/content/app/content_main_runner_app/content_main_runner_impl.o
```

Result:

```text
[102/102] CXX obj/content/app/content_main_runner_app/content_main_runner_impl.o
```

## Search hints

```bash
rg -n "ZygoteStarting|ZygoteForked|USE_ZYGOTE|use_zygote|content_main_runner_impl" /tmp/*.log docs/qnx/history/build-errors
```
