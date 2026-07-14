# cefsimple external network implementation plan (2026-07-13)

## Scope

This plan covers one issue observed after cefsimple content rendering became
available with the native QNX Screen/Mesa path:

1. External HTTPS sites appear to fail during name resolution.

The finding below is preliminary. It still requires bounded guest-side probes
before the final fix is selected.

## Initial findings

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

### Phase 2: TAP network provisioning

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

## Validation

Build gates:

- Re-bootstrap the managed patch stack on a clean Chromium base.
- Build the touched QNX Ozone objects, then `cefsimple`, using
  `out/qnx_release/ninja_qnx.sh`.
- Ensure the generated Chromium copies match CEF-managed new files and that
  all managed patches apply cleanly.

Runtime network gates:

- Guest literal-IP connectivity, system name lookup, and HTTPS all pass
  independently.
- cefsimple loads `https://google.com` without resolver/proxy/certificate
  errors under the standard runner invocation.
- The existing NFS mount and deterministic local `file://` rendering still
  work.
- Re-running setup does not duplicate firewall state or overwrite an
  explicitly supplied resolver configuration unexpectedly.

## Stop conditions

Stop and revise the plan if literal external IP connectivity already succeeds
without host setup changes, or if the first cefsimple NetLog failure is not
DNS. Either outcome invalidates the leading environment-provisioning
hypothesis and selects a downstream TLS or routing fix instead.

Implement network provisioning as its own commit, separate from any pointer
input change.
