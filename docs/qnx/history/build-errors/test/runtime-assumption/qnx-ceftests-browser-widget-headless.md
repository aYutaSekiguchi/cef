# QNX ceftests browser-window startup stops at BrowserWidget

## Failure signature

Stage: test  
Category: runtime-assumption  
Scope: `ceftests`, Chrome/Views browser-window creation on QNX

Broad QNX `ceftests` execution stops as soon as the suite reaches the first
browser-window test:

```text
[==========] Running 1949 tests from 81 test suites.
...
[----------] 12 tests from AxViewportCollapseTest
[ RUN      ] AxViewportCollapseTest.CollapseDefault
__PI_QNX_EXIT__:139
FAIL: * (exit 139, 16.0s)
```

The same crash reproduces without the test runner's default environment and is
independent of `CHROME_EXE_PATH`:

```bash
export CHROME_EXE_PATH=/mnt/nfs/out/qnx_release/ceftests
./ceftests --gtest_filter=AxViewportCollapseTest.CollapseDefault
```

Core/backtrace:

```text
Program terminated with signal SIGSEGV, Segmentation fault.
#0 BrowserWidget::InitBrowserWidget()
   at ../../chrome/browser/ui/views/frame/browser_widget.cc:177
177 views::Widget::InitParams params = browser_native_widget_->GetWidgetParams(...)
```

## Root cause

The QNX `BrowserNativeWidgetFactory` stub returned `nullptr`:

```cpp
BrowserNativeWidget* BrowserNativeWidgetFactory::Create(
    BrowserWidget* browser_widget,
    BrowserView* browser_view) {
  return nullptr;
}
```

`BrowserWidget::InitBrowserWidget()` assumes the factory returns a valid
`BrowserNativeWidget`, so browser-window tests immediately dereferenced the
null pointer.

## Fix

Use the Aura browser native widget for QNX, matching the generic Aura path, and
provide a QNX `BrowserDesktopWindowTreeHost` backed by
`views::DesktopWindowTreeHostPlatform`:

- `chrome/browser/ui/views/frame/browser_native_widget_factory_qnx.cc`
- `chrome/browser/ui/views/frame/browser_desktop_window_tree_host_qnx.cc`
- `chrome/browser/ui/BUILD.gn`
- `ui/views/BUILD.gn` includes the platform desktop host implementation for QNX

The QNX `ceftests` runner also passes headless-friendly flags because QNX has no
native Ozone window-system implementation yet:

```text
--ozone-platform=headless --disable-gpu --disable-gpu-compositing
```

## Verification

Build:

```bash
./out/qnx_release/ninja_qnx.sh ceftests
```

Result: build completed and linked `ceftests`.

The original SIGSEGV is fixed. After the change, the same narrow test no longer
crashes in `BrowserWidget::InitBrowserWidget`; it exits with the next runtime
blocker instead:

```text
./tools/qnx_run_test.sh --ceftests 'AxViewportCollapseTest.CollapseDefault'
...
[ RUN      ] AxViewportCollapseTest.CollapseDefault
__PI_QNX_EXIT__:1
```

The full suite also reaches the same next blocker with exit 1 instead of the
previous exit 139 null dereference.

## Remaining blocker

Chrome-style browser tests now hit a follow-on runtime issue in preference
lookup / page load:

```text
Program terminated with signal SIGTRAP, Trace/breakpoint trap.
#0 ImmediateCrash()
#1 CheckFailure()
#2 PrefService::GetPreferenceValue()
```

Running with `--use-alloy-style` avoids that Chrome-style preference CHECK path
but still times out waiting for page/test completion, then crashes during
shutdown. Treat this as a separate browser-test runtime blocker after the
BrowserNativeWidget null dereference is resolved.
