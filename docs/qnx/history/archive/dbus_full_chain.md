# Dependency Chain to //dbus

## Error Summary

```
//dbus/BUILD.gn:13:1: Assertion failed.
assert(use_dbus)
^-----
See //components/dbus/xdg/BUILD.gn:27:5: which caused the file to be included.
    "//dbus",
    ^-------
Toolchain: //build/toolchain/qnx:clang_x64
```

## Root Cause

`//dbus/BUILD.gn` line 11-13:
```python
if (!use_dbus) {
  print_stack_trace()
}
assert(use_dbus)
```

`use_dbus` is defined in `//build/config/features.gni` line 37:
```python
use_dbus = is_linux || is_chromeos
```

The QNX toolchain (`//build/toolchain/qnx:clang_x64`) sets `current_os = "qnx"`, so `is_linux = false`, causing `use_dbus = false`.

## Dependency Chain

```
gn_all / chrome (or CEF)
  └── //chrome/browser:BUILD.gn:6969
      └── //components/dbus:BUILD.gn:14
          └── //components/dbus/xdg:BUILD.gn:27
              └── //dbus:BUILD.gn:11 ← assert(use_dbus) fails here

Alternative paths from ui/:
  ├── //ui/base:BUILD.gn:1323
  │   └── //components/dbus:BUILD.gn:14
  │       └── //components/dbus/xdg:BUILD.gn:27
  │           └── //dbus:BUILD.gn:11
  │
  ├── //ui/base/x:BUILD.gn:100
  │   └── //components/dbus:BUILD.gn:14
  │       └── //components/dbus/xdg:BUILD.gn:27
  │           └── //dbus:BUILD.gn:11
  │
  ├── //ui/ozone/platform/wayland:BUILD.gn:327
  │   └── //components/dbus:BUILD.gn:14
  │       └── //components/dbus/xdg:BUILD.gn:27
  │           └── //dbus:BUILD.gn:11
  │
  └── //ui/ozone/platform/x11:BUILD.gn:103
      └── //components/dbus:BUILD.gn:14
          └── //components/dbus/xdg:BUILD.gn:27
              └── //dbus/BUILD.gn:11

Other consumers of //components/dbus:
  ├── //ui/shell_dialogs:BUILD.gn:68,179
  ├── //components/power_monitor:BUILD.gn:19
  ├── //components/printing/common:BUILD.gn:44,87
  ├── //components/system_media_controls:BUILD.gn:53,95
  ├── //services/device:BUILD.gn:464
  ├── //device/bluetooth:BUILD.gn:606
  └── (others listed in grep output)
```

## Key Files and Line Numbers

| File | Line | Content |
|------|------|---------|
| `dbus/BUILD.gn` | 11-13 | `if (!use_dbus) { print_stack_trace() } assert(use_dbus)` |
| `components/dbus/BUILD.gn` | 14 | `"//components/dbus/xdg",` (conditional on `is_linux`) |
| `components/dbus/xdg/BUILD.gn` | 27 | `"//dbus",` |
| `build/config/features.gni` | 37 | `use_dbus = is_linux \|\| is_chromeos` |
| `build/toolchain/qnx/BUILD.gn` | 33 | `current_os = "qnx"` |

## Conditional Logic in each file

**components/dbus/BUILD.gn** (lines 14-15):
```python
if (is_linux) {
  public_deps += [
    "//components/dbus/menu",
    "//components/dbus/thread_linux",
    "//components/dbus/xdg",  # ← triggers chain
  ]
}
```

**components/dbus/xdg/BUILD.gn** (lines 26-27):
```python
deps = [
  ...
  "//dbus",  # ← asserts here on QNX
]
```

## Solution Options

1. **Modify QNX toolchain args** to set `is_linux = true` (workaround)
2. **Guard //dbus with condition** in `components/dbus/xdg/BUILD.gn`
3. **Use conditional deps** in `components/dbus/xdg` to skip //dbus when `!use_dbus`