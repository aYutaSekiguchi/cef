# QNX QEMU guest has no external network when TAP setup is stale

- Date: 2026-07-17
- Signature: `NSSWITCH(_nsdispatch): getaddrinfo` / `curl: EXIT=6` or `EXIT=28`
- Stage: `test`
- Category: `test-environment`
- Scope: QEMU TAP/NAT/DNS setup for live cefsimple content

## Symptoms

`cefsimple` could display the Google page already present in the image, but
searches and direct navigation did not change the page. In the affected host
state, `tap0` was `NO-CARRIER`/`state DOWN`. The guest had no usable default
route or nameserver. Direct guest checks failed as follows:

- `curl https://93.184.216.34/`: timeout (`EXIT=28`)
- `curl https://www.google.com`: name resolution failure (`EXIT=6`)
- repeated `NSSWITCH`/`getaddrinfo` failures appeared in the QNX log

## Root cause

The host-side `qnx_setup_env.sh` prerequisite had not been successfully run
because it requires interactive `sudo` authorization. The stale TAP interface
alone is not sufficient: forwarding, NAT, and the guest-visible resolver must
also be configured.

## Fix pattern

Run the documented host setup after reboot or firewall changes:

```bash
sudo ./cef/tools/qnx_setup_env.sh
```

Then verify the gateway, external address, and DNS layers independently before
diagnosing Chromium. Do not treat a cached page render as proof of network
connectivity.

## Applied change

No source change was made. This is an environment prerequisite issue.

## Verification

The GUI harness confirmed that the QNX screen remained responsive and the
HasCapture crash fix remained effective, while network-level navigation still
failed. QEMU was stopped after the checks.

## Files touched

- `tools/qnx_setup_env.sh` (existing setup entrypoint)
- `docs/qnx/testing.md` (existing network verification commands)
- `docs/qnx/gui-testing.md` (existing GUI workflow)

## Related notes

- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-cefsimple-search-hascapture-trap.md`
