# QNX content child process launcher implementation missing

## Failure signature

Stage: link
Category: platform-api-gap / build-graph
Target: `//cef:cefsimple` via `./libcef.so`

After the QNX TTS backend fix, the first unresolved content symbols were child process launcher platform methods:

```text
./libcef.so: undefined reference to `content::internal::ChildProcessLauncherHelper::TerminateProcess(base::Process const&, int)'
./libcef.so: undefined reference to `content::internal::ChildProcessLauncherHelper::BeforeLaunchOnClientThread()'
./libcef.so: undefined reference to `content::internal::ChildProcessLauncherHelper::LaunchProcessOnLauncherThread(...)'
./libcef.so: undefined reference to `content::internal::ChildProcessLauncherHelper::GetTerminationInfo(...)'
```

## Root cause

`content/browser/child_process_launcher_helper.cc` declares platform-specific methods that are implemented by OS-specific files. QNX was not compiling any implementation file for these methods.

The Linux implementation cannot be reused directly because it includes and wires Chromium's Linux zygote/sandbox stack (`SandboxHostLinux`, `ZygoteHostImpl`, Linux sandbox policy). QNX disables zygote and does not provide that Linux sandbox path.

## Fix

Files:

- New file: `cef/patch/qnx/chromium/new_files/content/browser/child_process_launcher_helper_qnx.cc`
- Patch: `cef/patch/patches/qnx/chromium/content_child_process_launcher_qnx.patch`

The QNX implementation uses the common POSIX file-descriptor mapping helper and launches child processes with `base::LaunchProcess`, without zygote or Linux sandbox integration.

`content/browser/BUILD.gn` now includes:

```gn
if (is_qnx) {
  sources += [ "child_process_launcher_helper_qnx.cc" ]
}
```

## Verification

After GN regeneration, `out/qnx_release/obj/cef/libcef.ninja` contains:

```text
child_process_launcher_helper_qnx.o
```

and does not contain:

```text
child_process_launcher_helper_linux.o
```

A targeted object build was started:

```bash
ninja -C out/qnx_release obj/content/browser/browser/child_process_launcher_helper_qnx.o
```

It produced no diagnostics before the local timeout while completing prerequisite generated targets.

## Search hints

```bash
rg -n "ChildProcessLauncherHelper::TerminateProcess|child_process_launcher_helper_qnx|LaunchProcessOnLauncherThread|GetTerminationInfo" docs/qnx/history/build-errors
```
