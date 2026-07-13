# cefsimple input and external network implementation plan (2026-07-13)

## Scope

This plan covers two issues observed after cefsimple content rendering became
available with the native QNX Screen/Mesa path:

1. Pointer input does not reach either the cefsimple Views controls or web
   content.
2. External HTTPS sites appear to fail during name resolution.

The findings below are preliminary. The input finding is confirmed in source;
the network finding still requires bounded guest-side probes before selecting
the final fix.

## Initial findings

### Pointer input

`QnxPlatformEventSource` polls `screen_get_event()` every 16 ms and recognizes
`SCREEN_EVENT_POINTER`, but the pointer branch only reads position/buttons and
emits `DLOG(INFO)`. It explicitly defers `ui::MouseEvent` construction and does
not call `DispatchEvent()`. `QnxWindow` is not currently a
`PlatformEventDispatcher`, so there is also no standard Ozone window target to
receive a dispatched event. The absent logs in a Release build are expected
because the current diagnostics use `DLOG`/`VLOG`.

The current CEF-managed new-file source and the generated Chromium file are
identical. This is unfinished Phase 4 input work, not a regression introduced
by the render-only fallback.

Lower-probability prerequisites still need runtime confirmation: Screen must
actually deliver pointer events under the current QEMU device configuration,
and the event-source polling task must remain active on the UI sequence.

### External network and DNS

The runner configures a TAP guest at `10.0.2.2`, a default route through
`10.0.2.1`, and NFS. It does not configure `/etc/resolv.conf`. The host setup
creates `tap0` and starts NFS, but does not enable IPv4 forwarding or install a
NAT rule. Because the runner uses `-netdev tap`, QEMU user-network DNS/NAT is
not available.

The most likely failure is therefore environment provisioning: absent or
unreachable DNS, potentially preceded by absent host forwarding/NAT. Chromium
uses the QNX system resolver path, so an unusable guest resolver configuration
can surface as `ERR_NAME_NOT_RESOLVED`. This is not yet proven: the current
runtime evidence only validated `file://` content, and guest IP/DNS/TLS probes
have not been captured.

## Implementation sequence

### Phase 1: bounded diagnostics

Input:

- Temporarily make one polling-start marker and rate-limited pointer marker
  visible in Release logging. Include Screen event type, source window,
  position, button state, and property-read return codes.
- Confirm pointer events arrive when QEMU receives mouse movement/clicks.
- Confirm the source `screen_window_t` matches a registered
  `QnxWidgetRecord::screen_win`.
- If no pointer event arrives, inspect QEMU input devices and Screen domain
  configuration before implementing Chromium dispatch.

Network, in this strict order:

1. Record `ifconfig`, routes, and `/etc/resolv.conf` in the guest.
2. Test guest-to-host (`10.0.2.1`) and external IPv4 connectivity by literal
   address. Failure here selects host forwarding/NAT work, not DNS work.
3. Test UDP/TCP DNS reachability and QNX `getaddrinfo()`/available lookup tools.
4. Test TLS to a literal address with SNI, if an SSL client is available.
5. Run cefsimple with a NetLog and classify the first failing phase as DNS,
   connect, proxy, or certificate validation.

All probes must be timeout-bounded and use the working cefsimple launch
baseline: native CEF window mode, no sandbox, and system `libEGL.so.1`
preloaded. Record the exact guest commands and results in the runtime matrix.

### Phase 2: pointer MVP

- Add lookup by `screen_window_t` to `QnxWindowManager`; retain screen-point
  hit testing as a fallback only for events without a source window.
- Make `QnxWindow` a `PlatformEventDispatcher`, register/unregister it with the
  active `PlatformEventSource`, and forward accepted events through
  `PlatformWindowDelegate::DispatchEvent()`.
- Translate Screen pointer state into Chromium mouse move, press, and release
  events. Track the previous button mask so transitions are emitted exactly
  once, map QNX button bits to Chromium flags, and preserve both local and root
  coordinates.
- Set the target window before dispatch, following the existing Ozone backend
  contract. Respect capture when it is subsequently implemented; do not fake
  capture in the first patch.
- Keep touch, wheel, keyboard, IME, cursor confinement, and multi-window
  activation outside this MVP unless compilation requires a small shared
  abstraction.

Durable sources:

- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_platform_event_source.{h,cc}`
- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.{h,cc}`
- `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.{h,cc}`

### Phase 3: TAP network provisioning

Select this phase only after Phase 1 identifies the failing layer.

- If literal-address connectivity fails, extend `qnx_setup_env.sh` to enable
  IPv4 forwarding and install an idempotent NAT/MASQUERADE rule for
  `10.0.2.0/24`. Detect the host firewall backend and verify the installed rule
  instead of appending duplicates on every run.
- If external IP works but system lookup fails, add a configurable guest DNS
  server to `qnx_run.sh` and provision `/etc/resolv.conf` during guest setup.
  Do not pass a host loopback resolver such as `127.0.0.53`; reject loopback
  values and allow an explicit `--dns-server` override.
- If QNX libc does not honor `/etc/resolv.conf`, document the QNX resolver
  requirement and provision its supported configuration rather than adding a
  Chromium host-resolver workaround.
- Keep TAP networking because the current NFS workflow depends on the
  host/guest subnet. Treat switching to QEMU user networking as a separate
  design option, not the first fix.
- Do not use `--host-resolver-rules`, a fixed IP for google.com, proxy flags, or
  disabled certificate checks as production fixes. They may be used only to
  isolate a layer during testing.

Durable sources:

- `cef/tools/qnx_setup_env.sh` for host forwarding/NAT
- `cef/tools/qnx_run.sh` for guest resolver provisioning and diagnostics
- `cef/docs/qnx/testing.md` for operator prerequisites and verification

### Phase 4: validation and cleanup

Build gates:

- Re-bootstrap the managed patch stack on a clean Chromium base.
- Build the touched QNX Ozone objects, then `cefsimple`, using
  `out/qnx_release/ninja_qnx.sh`.
- Ensure the generated Chromium copies match CEF-managed new files and that
  all managed patches apply cleanly.

Runtime input gates:

- A click on reload triggers exactly one press and one release and reloads the
  page.
- Tabs and other cefsimple Views controls respond to clicks.
- Web-content pointer handlers receive move/down/up at correct coordinates.
- Moving outside the window, rapid clicks, and resize do not crash or produce
  stuck button state.
- Release diagnostics prove the path once, then noisy temporary logging is
  removed or reduced to `VLOG` after validation.

Runtime network gates:

- Guest literal-IP connectivity, system name lookup, and HTTPS all pass
  independently.
- cefsimple loads `https://google.com` without resolver/proxy/certificate
  errors under the standard runner invocation.
- The existing NFS mount and deterministic local `file://` rendering still
  work.
- Re-running setup does not duplicate firewall state or overwrite an
  explicitly supplied resolver configuration unexpectedly.

## Recommended order and stop conditions

Implement pointer input and network provisioning as separate commits. Start
with both Phase 1 diagnostic passes; then implement the pointer MVP while the
network result selects NAT, resolver provisioning, or a downstream TLS fix.
Stop and revise the plan if Screen emits no pointer events, if literal external
IP connectivity already succeeds without host setup changes, or if the first
cefsimple NetLog failure is not DNS. These outcomes invalidate the respective
leading hypothesis.
