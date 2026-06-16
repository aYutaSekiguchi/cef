# QNX: WebApp isolated-app scheme guard

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `//chrome/browser/web_applications:web_applications`
- file: `chrome/browser/web_applications/web_app.cc`

## Failure signature

Initial failure after enabling QNX in the desktop web app build graph:

```text
../../chrome/browser/web_applications/web_app.cc:761:3: error: unterminated function-like macro invocation
  CHECK(manifest_id_.is_valid()
  ^
../../chrome/browser/web_applications/web_app.cc:1475:24: error: expected '}'
}  // namespace web_app
                       ^
```

After adding QNX only to the `CHECK` expression guard, the follow-up failure exposed the matching include guard problem:

```text
../../chrome/browser/web_applications/web_app.cc:764:43: error: no member named 'kIsolatedAppScheme' in namespace 'webapps'
        && manifest_id_.SchemeIs(webapps::kIsolatedAppScheme))
                                  ~~~~~~~~~^~~~~~~~~~~~~~~~~~
```

## Root cause

`WebApp::SetIsolationData()` has a platform-guarded `CHECK` expression where the closing parenthesis for `CHECK(...)` is inside the Win/Mac/Linux/ChromeOS branch:

```cpp
CHECK(manifest_id_.is_valid()
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX) || \
    BUILDFLAG(IS_CHROMEOS)
      && manifest_id_.SchemeIs(webapps::kIsolatedAppScheme))
#endif
    ;
```

QNX reaches this desktop web app code once the broader web app BUILD.gn asserts are extended to include `is_qnx`. Without QNX in the guard, the preprocessor removes the branch that closes the `CHECK` invocation. When only the expression guard is fixed, QNX still misses the matching guarded include of `components/webapps/isolated_web_apps/scheme.h`, so `webapps::kIsolatedAppScheme` remains undeclared.

## Fix

Patch: `qnx/chromium/web_app_isolated_app_scheme_guard_qnx`

Add `BUILDFLAG(IS_QNX)` to both related guards in `web_app.cc`:

- the include guard for `components/webapps/isolated_web_apps/scheme.h`
- the `WebApp::SetIsolationData()` `CHECK` expression guard

This keeps QNX on the same desktop/IWA path as Linux without changing behavior on other platforms.

## Verification

Narrow reproducer passed:

```bash
cd /home/yuta/chromium/test/src/out/qnx_release
source qnx_env.sh
ninja -C . obj/chrome/browser/web_applications/web_applications/web_app.o
```

Result:

```text
[100/100] CXX obj/chrome/browser/web_applications/web_applications/web_app.o
```

## Search hints

```bash
rg -n "unterminated function-like macro invocation|kIsolatedAppScheme|SetIsolationData|web_app.cc" /tmp/*.log docs/qnx/history/build-errors
```
