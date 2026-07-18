# Rootless QEMU payload delivery

`cef/tools/qnx_run.sh` defaults to a fully unprivileged runtime path:

1. `qnx_payload_http.sh` creates a cached tarball containing the requested
   executable, or the `cefsimple` runtime manifest.
2. QEMU starts its native `passt` network backend as the current user.
3. A short-lived Python HTTP server exposes only the tarball directory.
4. The QNX guest downloads the archive from passt's host address
   (`192.168.0.1`) and extracts it into the 32 GB `/data` filesystem.
5. The runner executes the command from `/data/qnx_payload/payload` and stops
   QEMU and the HTTP server on exit.

This avoids privileged TAP/NAT configuration, NFS exports, and the guest's
small read-only root filesystem. Tar preserves executable modes and symlinks.

## Examples

```bash
# A single test binary is inferred from the command.
./cef/tools/qnx_run.sh -- ./base_unittests --gtest_filter=ProcessTest.Create

# cefsimple selects its runtime manifest automatically.
./cef/tools/qnx_run.sh -- ./cefsimple --no-sandbox

# Supply a custom payload explicitly.
./cef/tools/qnx_run.sh \
  --http-payload-file /absolute/path/to/helper.dat \
  --http-payload-file /absolute/path/to/test_binary \
  -- ./test_binary
```

The archive is fingerprinted and reused when its complete input contents are
unchanged. Override its location with `--http-payload-image`; select a fixed
listener with `--http-payload-port` when necessary.

## Legacy compatibility

The privileged workflow remains explicit:

```bash
sudo ./cef/tools/qnx_setup_env.sh
./cef/tools/qnx_run.sh --net-backend tap --payload-mode nfs -- ./base_unittests
```

Use it only for workflows that specifically require a live NFS view of the
entire Chromium checkout.
