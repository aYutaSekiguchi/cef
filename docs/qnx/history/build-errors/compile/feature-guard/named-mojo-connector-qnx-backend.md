# named_mojo_ipc_server endpoint connector needs a QNX-specific backend

- Date: 2026-06-09
- Signature: `no member named 'require_same_peer_user' in 'named_mojo_ipc_server::EndpointOptions'`
- Stage: compile
- Category: feature-guard
- Scope: `components/named_mojo_ipc_server`

## Symptoms

After the `ui/events/keycode_converter.cc` QNX branch, `cefsimple` advanced to:

```text
FAILED: obj/components/named_mojo_ipc_server/named_mojo_ipc_server/named_mojo_server_endpoint_connector_linux.o
named_mojo_server_endpoint_connector_linux.cc:77: error: no member named 'require_same_peer_user' in 'named_mojo_ipc_server::EndpointOptions'
named_mojo_server_endpoint_connector_linux.cc:88-94: error: no member named 'credentials' in 'named_mojo_ipc_server::ConnectionInfo'
named_mojo_server_endpoint_connector_linux.cc:89: error: use of undeclared identifier 'SO_PEERCRED'
```

## Root cause

`components/named_mojo_ipc_server/BUILD.gn` selects the Linux-only connector backend whenever `is_linux`:

```gn
if (is_linux) {
  sources += [ "named_mojo_server_endpoint_connector_linux.cc" ]
}
```

In this QNX port `is_linux` is also true, so the Linux TU is compiled. But:

- `EndpointOptions::require_same_peer_user` is only defined under `BUILDFLAG(IS_LINUX)`.
- `ConnectionInfo::credentials` (`struct ucred`) is only defined under `BUILDFLAG(IS_LINUX)`.
- QNX SDP 8.0 sysroot `<sys/socket.h>` does not expose `SO_PEERCRED` or `struct ucred`.

Therefore the Linux TU cannot be reused on QNX.

## Fix pattern

Mirror the existing platform-specific backends. Add a QNX-only connector:

- new file `components/named_mojo_ipc_server/named_mojo_server_endpoint_connector_qnx.cc`
- `BUILD.gn`: select it for `is_qnx` after the Linux branch
- `EndpointOptions::require_same_peer_user` accepts `BUILDFLAG(IS_QNX)` so cross-platform callers continue to compile

The QNX backend uses the same POSIX named-socket primitives (`SOCK_STREAM` + `bind()` + `accept()`), but skips peer-credential introspection. `ConnectionInfo` only contains `pid` on QNX.

## Applied change

- `components/named_mojo_ipc_server/endpoint_options.h`
  ```diff
  -#if BUILDFLAG(IS_LINUX)
  +#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)
     bool require_same_peer_user = true;
   #endif
  ```
- `components/named_mojo_ipc_server/BUILD.gn`
  ```diff
   if (is_linux) {
     sources += [ "named_mojo_server_endpoint_connector_linux.cc" ]
  +} else if (is_qnx) {
  +  sources += [ "named_mojo_server_endpoint_connector_qnx.cc" ]
   } else if (is_win) { ... }
  ```
- New managed file:
  - `cef/patch/qnx/chromium/new_files/components/named_mojo_ipc_server/named_mojo_server_endpoint_connector_qnx.cc`

## Verification

- Clean-tree bootstrap succeeds:
  - `git checkout -f`: `0`
  - `gclient sync -f -R`: `0`
  - `./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root /home/yuta/qnx800`: `0`
- Post-bootstrap source selection:
  - `BUILD.gn` selects Linux backend only for `(is_linux && !is_qnx) || is_chromeos`
  - `BUILD.gn` selects the QNX backend for `is_qnx`
  - `named_mojo_server_endpoint_connector_qnx.cc` is installed
  - `EndpointOptions::require_same_peer_user` is now defined under `BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)`
- `./out/qnx_release/ninja_qnx.sh cefsimple` removes all the original failure signatures:
  - `named_mojo_server_endpoint_connector_linux` build targets: `0`
  - `named_mojo_server_endpoint_connector_qnx` build targets: `1`
  - `require_same_peer_user' in 'named_mojo_ipc_server::EndpointOptions`: `0`
  - `undeclared identifier 'SO_PEERCRED'`: `0`
  - `credentials' in 'named_mojo_ipc_server::ConnectionInfo`: `0`
  - `undeclared identifier 'HANDLE_EINTR'`: `0`
- The next visible blocker in this family is `linux/rtnetlink.h` in
  `services/network/public/cpp/network_interface_change_listener_mojom_traits.h` and
  its mojom-side fallout, as recorded in
  `qnx/history/build-errors/compile/feature-guard/ui-events-keycode-converter-qnx-scan-codes.md`.

## Files touched

- `cef/patch/patches/qnx/chromium/named_mojo_server_endpoint_connector_qnx_qnx_impl.patch`
- `cef/patch/qnx/chromium/new_files/components/named_mojo_ipc_server/named_mojo_server_endpoint_connector_qnx.cc`
- `cef/patch/patch.cfg`
- `cef/docs/qnx/history/build-errors/compile/feature-guard/named-mojo-connector-qnx-backend.md`
- `cef/docs/qnx/build-error-index.md`
