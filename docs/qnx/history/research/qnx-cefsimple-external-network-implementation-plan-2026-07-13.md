# cefsimple external network implementation plan (2026-07-13)

## Scope

This plan covers one issue observed after cefsimple content rendering became
available with the native QNX Screen/Mesa path:

1. External HTTPS sites appear to fail during name resolution.

The finding below is preliminary. It still requires bounded guest-side probes
before the final fix is selected.

## Investigation and implementation update (2026-07-15)

The bounded QEMU probe is now complete. It ran without sudo, without killing
an existing QEMU session, and without changing tracked files:

- `ping 10.0.2.1` succeeds, so the TAP link and guest route are healthy.
- `ping 8.8.8.8` fails, confirming missing host forwarding/NAT.
- `/etc/resolv.conf` is a writable symlink to
  `/data/var/dhcpcd/resolv.conf`.
- QNX `getent` honors a nameserver written to `/etc/resolv.conf`; the
  resolver-compatibility contingency is not needed for this image.
- With no reachable external path, DNS configured to `8.8.8.8` remains
  unreachable, so DNS must be fixed after host forwarding.

The working-tree implementation now provisions both layers:

- `tools/qnx_setup_env.sh` enables IPv4 forwarding and installs checked,
  idempotent iptables FORWARD and MASQUERADE rules for `tap0`. The rules are
  restricted to guest-originated traffic and established return traffic, and
  use the host's iptables-nft integration when available.
- `tools/qnx_run.sh` and `tools/qnx_run_test.sh` discover the first usable
  non-loopback host resolver via `resolvectl`, with `QNX_DNS_SERVER` and
  `--dns-server` overrides. The address is written after the guest route is
  configured.
- The manual guest setup and testing documentation now include the resolver
  step and layer-by-layer probes.

Static checks and runtime network validation pass. The changed
`qnx_run.sh` path successfully reached `8.8.8.8`, wrote
`nameserver 192.168.0.1`, and resolved `google.com`. The changed
`qnx_run_test.sh`/Python path was also exercised end-to-end and resolved
`google.com`.

The parent tree was then reset using the documented clean-tree recovery,
QNX source sync was run with `--nohooks` for the depot_tools
`source_tarball` evaluator bug, and the CEF bootstrap completed with
`492 patches total (472 applied, 20 skipped, 0 failed)`. `cefsimple` built
successfully through `[54895/54895] LINK ./cefsimple`.

A corrected fresh-binary smoke test loaded
`https://www.google.com/blank.html` with HTTP 200 and rendered frames through
the QNX Screen/Mesa pipeline. The earlier failed smoke command used the guest
`timeout` utility, which is absent from the QNX image; use the runner's
`--timeout` option instead. QEMU cleanup was successful. Long-running
stability and NetLog parsing remain optional follow-up work.

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

Current status:

- Completed: bounded guest probe, TAP/link classification, resolver behavior
  check, Bash/Python syntax checks, CLI parser checks, iptables rule
  translation checks, post-setup literal-IP/NAT/DNS validation through both
  runners, clean QNX bootstrap with 0 failed patches, fresh `cefsimple` build,
  HTTPS HTTP-200 smoke test, QNX Screen/Mesa frame submission, and QEMU
  cleanup verification.
- Follow-up only: firewall persistence across reboot and long-running
  cefsimple stability are not covered; rerun `qnx_setup_env.sh` after reboot
  or firewall reload. The NetLog artifact was created but root-owned on NFS
  and was not parsed.

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
