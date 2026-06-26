# QNX ceftests Chrome-style browser startup needs DevTools policy list prefs

## Failure signature

Stage: test  
Category: runtime-assumption  
Scope: `ceftests`, Chrome-style browser creation on QNX

After fixing the QNX `BrowserNativeWidgetFactory` null stub, Chrome-style
browser tests progressed to a CHECK failure during browser startup:

```text
[ RUN      ] BrowserSettingsTest.JavaScriptDisabled
__PI_QNX_EXIT__:133
```

A temporary probe in `PrefService::GetPreferenceValue()` identified the
unregistered pref:

```text
QNX unregistered pref probe: devtools.availability_allowlist
```

Core/backtrace for the CHECK:

```text
Program terminated with signal SIGTRAP, Trace/breakpoint trap.
#0 ImmediateCrash()
#1 CheckFailure()
#2 PrefService::GetPreferenceValue()
```

## Root cause

`DeveloperToolsPolicyCheckerFactory::RegisterProfilePrefs()` registered
`prefs::kDeveloperToolsAvailabilityAllowlist` and
`prefs::kDeveloperToolsAvailabilityBlocklist` only for Linux, macOS, Windows,
and ChromeOS:

```cpp
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS)
```

QNX is using the desktop Chrome/DevTools policy checker path in `ceftests`, so
it also needs those desktop DevTools policy list prefs registered.

## Fix

Include QNX in the desktop pref-registration guard:

```cpp
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_QNX)
```

Managed patch:

```text
cef/patch/patches/qnx/chromium/chrome_browser_policy_devtools_prefs_qnx.patch
```

and register it in:

```text
cef/patch/patch.cfg
```

## Verification

Build:

```bash
./out/qnx_release/ninja_qnx.sh ceftests
```

Result: success.

Narrow test after the fix:

```bash
./tools/qnx_run_test.sh --ceftests 'BrowserSettingsTest.JavaScriptDisabled'
```

The previous `SIGTRAP`/`PrefService::GetPreferenceValue()` CHECK is gone.
The test now reaches the next runtime blocker and exits with `1` / later
shutdown instability instead of the unregistered-pref CHECK.

## Remaining blocker

Browser content still does not complete loading under the current QNX headless
configuration. For example:

```text
[ RUN      ] BrowserSettingsTest.JavaScriptDisabled
[...:ERROR:third_party/webrtc/rtc_base/cpu_info.cc:73] No function to get number of cores
__PI_QNX_EXIT__:1
```

With `--use-alloy-style`, the same test times out waiting for `OnLoadEnd()` /
verification and then crashes during shutdown. Child process inspection shows
that GPU, network/storage utility, and renderer subprocesses are launched, so
this is no longer a simple process-launch failure. Continue investigation from
page-load / renderer/browser IPC completion under headless Ozone on QNX.
