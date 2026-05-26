# //dbus Dependency Analysis for QNX Target

## Summary

218 BUILD.gn files reference `//dbus`. However, `gn refs` fails because:
1. `dbus/BUILD.gn` has `assert(use_dbus)` at line 10
2. pkg-config for `dbus-1` is not available in the QNX build environment

**Key finding**: 52 places use `if (use_dbus)` guards, but 166 places unconditionally depend on `//dbus` or `//chromeos/ash/components/dbus/*`.

---

## Dependency Classification

### Category A: Can be disabled with GN args (`use_dbus=false`)

These have conditional guards in BUILD.gn:

| File | Lines | Description |
|------|-------|-------------|
| `content/browser/BUILD.gn` | 3671 | Linux/ChromeOS browser deps |
| `chrome/browser/BUILD.gn` | 6927, 6959 | Linux/ChromeOS browser deps |
| `chrome/browser/extensions/BUILD.gn` | 1273, 1479 | Extension dependencies |
| `chrome/browser/ui/BUILD.gn` | 3506, 5108, 5441 | UI components |
| `chrome/test/BUILD.gn` | 5633, 10084, 10619, 12519 | Test dependencies |
| `content/test/BUILD.gn` | 897, 3539 | Content test deps |
| `headless/BUILD.gn` | 538, 545 | Headless browser |
| `components/power_monitor/BUILD.gn` | 11, 20 | Power monitoring |
| `components/storage_monitor/BUILD.gn` | 59, 118, 160 | Storage monitoring |
| `components/printing/common/BUILD.gn` | 38, 46, 84, 88, 89 | Print dialog portal |
| `components/os_crypt/sync/BUILD.gn` | 77, 88, 95, 128, 166, 171 | OSCrypt sync |
| `components/os_crypt/async/browser/BUILD.gn` | 96, 111, 128, 147, 190, 202 | OSCrypt async |
| `services/device/BUILD.gn` | 80, 237, 463 | Device service |
| `services/device/wake_lock/BUILD.gn` | 39, 53 | Wake lock service |
| `services/device/battery/BUILD.gn` | 40 | Battery service |
| `services/device/geolocation/BUILD.gn` | 109 | Geolocation service |
| `ui/shell_dialogs/BUILD.gn` | 62, 176 | Shell dialogs |
| `ui/base/accelerator_listener/BUILD.gn` | 60 | Global accelerator |
| `ui/base/BUILD.gn` | 1320 | UI base |
| `ui/base/idle/BUILD.gn` | 71 | Idle detection |
| `ui/base/x/BUILD.gn` | 98 | X11 utilities |
| `ui/base/clipboard/BUILD.gn` | 68, 311 | Clipboard |
| `ui/ozone/platform/wayland/BUILD.gn` | 321 | Wayland platform |
| `device/bluetooth/BUILD.gn` | 604 | Bluetooth |
| `device/BUILD.gn` | 293 | Device core |
| `extensions/browser/api/networking_private/BUILD.gn` | 44 | Networking private API |
| `cef/BUILD.gn` | 1073 | CEF |
| `build/config/linux/dbus/BUILD.gn` | 14 | DBus config |
| `BUILD.gn` (root) | 489 | Top-level test deps |

### Category B: Requires BUILD.gn Modification (ChromeOS-Specific)

These are guarded by `is_chromeos` or `is_chromeos_device` assertions and cannot be disabled via GN args:

| File | Lines | Description |
|------|-------|-------------|
| `//chromeos/ash/components/dbus/BUILD.gn` | 11-80 | Main ash D-Bus component |
| `//chromeos/ash/components/dbus/*` | ~80 subdirs | All ChromeOS-specific D-Bus clients (anomaly_detector, attestation, audio, biod, chaps, cicerone, concierge, cryptohome, debug_daemon, etc.) |
| `//chromeos/dbus/*` | Various | ChromeOS D-Bus services |
| `//chromeos/ash/experiences/arc/BUILD.gn` | 295 | ARC experience |

**These require:**
- `assert(is_chromeos)` at line 2 of `//chromeos/ash/components/dbus/BUILD.gn`
- Cannot be compiled for QNX without heavy modification

### Category C: Unconditional Dependencies (Must Remove or Guard)

Files that reference `//dbus` without `use_dbus` checks:

| File | Lines | Note |
|------|-------|------|
| `chrome/browser/ash/main_parts/BUILD.gn` | 171 | ChromeOS Ash main parts |
| `chrome/browser/ash/login/screens/BUILD.gn` | 572 | Login screens |
| `chrome/browser/ash/login/saml/BUILD.gn` | 221 | SAML login |
| `chrome/browser/ash/policy/*/BUILD.gn` | Multiple | Policy components |
| `chrome/browser/ash/net/*/BUILD.gn` | Multiple | Network components |
| `chrome/browser/ash/dbus/BUILD.gn` | 74, 295, 341 | Ash D-Bus browser |
| `chrome/browser/ash/bluetooth/BUILD.gn` | 53, 98 | Bluetooth |
| `chrome/browser/ash/accessibility/BUILD.gn` | 82 | Accessibility |
| `chrome/browser/ash/system/BUILD.gn` | 145 | System components |
| `chrome/browser/ash/nearby/BUILD.gn` | 70 | Nearby |
| `chrome/browser/ash/schedqos/BUILD.gn` | 19, 32 | Scheduling QoS |
| `chromeos/ash/components/network/BUILD.gn` | 44, 238, 251, 334 | Network |
| `chromeos/ash/services/network_health/BUILD.gn` | 62 | Network health |
| `chromeos/ash/services/network_config/BUILD.gn` | 32 | Network config |
| `chromeos/ash/services/cellular_setup/BUILD.gn` | 25, 51, 71, 123 | Cellular setup |
| `components/dbus/*/BUILD.gn` | Various | D-Bus component wrappers (menu, properties, thread_linux, utils, xdg) |

---

## Recommendations for QNX

### High Priority (Critical Path)

1. **Set `use_dbus=false`** in GN args - disables most Category A deps
   
2. **Guard or remove `//chromeos/ash/components/dbus` directory**
   - Add `if (!is_chromeos_device)` guards
   - Or create stub library to satisfy deps
   
3. **Fix unconditional `//dbus` references in ChromeOS ash components**
   - `chrome/browser/ash/*` requires `assert(is_chromeos)` guards
   - `chromeos/ash/*` requires `assert(is_chromeos)` guards

### Medium Priority

4. **Create `//chromeos/ash/components/dbus:dbus_stub`** - empty group for non-ChromeOS
5. **Create `//dbus:dbus_stub`** - empty component when `use_dbus=false`

### Files that need BUILD.gn modifications

```
//chromeos/ash/components/dbus/BUILD.gn
//chromeos/ash/components/dbus/*/BUILD.gn
//chromeos/dbus/*/BUILD.gn
//chrome/browser/ash/*/BUILD.gn (multiple files)
```

### GN args that can help

```python
use_dbus = false
is_chromeos = false  # May break other ChromeOS logic
is_chromeos_device = false
```

---

## Verification Commands

```bash
# Show all files depending on //dbus
cd <CHROMIUM_SRC_ROOT>
grep -rn "//dbus" --include="BUILD.gn" 2>/dev/null | wc -l  # 218 references

# Show conditional uses
grep -rn "if.*use_dbus" --include="BUILD.gn" 2>/dev/null | wc -l  # 52 uses

# Show ChromeOS-specific deps (require BUILD.gn changes)
grep -rl "assert(is_chromeos" --include="BUILD.gn" chromeos/
```

---

## Status

- `gn gen` fails: assert(use_dbus) in dbus/BUILD.gn:10
- `gn refs` requires successful `gn gen`
- Build will fail without either:
  1. `use_dbus=true` (requires system D-Bus headers)
  2. Patching all `//dbus` deps to be conditional
  3. Creating stub libraries