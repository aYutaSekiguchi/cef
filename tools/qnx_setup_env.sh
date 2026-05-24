#!/usr/bin/env bash
# QNX QEMU test environment setup.
# Run once after reboot to restore NFSv3 export + TAP networking.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHROMIUM_SRC="${CHROMIUM_SRC:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
EXPORT_DIR="/export/chromium-src"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "ERROR: This script must be run as root." >&2
  echo "Use: sudo $SCRIPT_DIR/qnx_setup_env.sh" >&2
  exit 1
fi

if [[ ! -d "$CHROMIUM_SRC" ]]; then
  echo "ERROR: Chromium source tree not found: $CHROMIUM_SRC" >&2
  exit 1
fi

TAP_USER="${SUDO_USER:-$USER}"

echo "=== QNX QEMU Test Environment Setup ==="
echo "Chromium src: $CHROMIUM_SRC"
echo "Export dir:    $EXPORT_DIR"
echo

# 1. Enable NFS v3.
echo "[1/5] Enable NFS v3"
if grep -Eq '^[[:space:]]*#?[[:space:]]*vers3[[:space:]]*=' /etc/nfs.conf; then
  sed -i -E 's/^[[:space:]]*#?[[:space:]]*vers3[[:space:]]*=.*/vers3=y/' /etc/nfs.conf
else
  awk '
    /^\[nfsd\]$/ { print; print "vers3=y"; inserted=1; next }
    { print }
    END { if (!inserted) { print "[nfsd]"; print "vers3=y" } }
  ' /etc/nfs.conf > /etc/nfs.conf.tmp
  mv /etc/nfs.conf.tmp /etc/nfs.conf
fi
grep -Eq '^vers3[[:space:]]*=[[:space:]]*y' /etc/nfs.conf || {
  echo "ERROR: failed to enable NFS v3" >&2
  exit 1
}

# 2. Bind-mount chromium src to export path.
echo "[2/5] Prepare bind mount + export"
mkdir -p "$EXPORT_DIR"
if mountpoint -q "$EXPORT_DIR"; then
  CURRENT_SOURCE="$(findmnt -n -o SOURCE --target "$EXPORT_DIR" 2>/dev/null || true)"
  if [[ "$CURRENT_SOURCE" != "$CHROMIUM_SRC" ]]; then
    umount "$EXPORT_DIR"
    mount --bind "$CHROMIUM_SRC" "$EXPORT_DIR"
  fi
else
  mount --bind "$CHROMIUM_SRC" "$EXPORT_DIR"
fi

FSTAB_LINE="$CHROMIUM_SRC $EXPORT_DIR none bind 0 0"
if grep -Eq "^[^#]+[[:space:]]+$EXPORT_DIR[[:space:]]+" /etc/fstab; then
  sed -i -E "s|^[^#]+[[:space:]]+$EXPORT_DIR[[:space:]]+.*|$FSTAB_LINE|" /etc/fstab
else
  echo "$FSTAB_LINE" >> /etc/fstab
fi

EXPORT_LINE="$EXPORT_DIR *(rw,no_root_squash,async,insecure)"
if grep -Eq "^[[:space:]]*$EXPORT_DIR[[:space:]]+" /etc/exports; then
  sed -i -E "s|^[[:space:]]*$EXPORT_DIR[[:space:]]+.*|$EXPORT_LINE|" /etc/exports
else
  echo "$EXPORT_LINE" >> /etc/exports
fi

# 3. Setup TAP.
echo "[3/5] Setup tap0"
if ! ip link show tap0 >/dev/null 2>&1; then
  ip tuntap add dev tap0 mode tap user "$TAP_USER"
fi
if ! ip addr show tap0 | grep -q '10.0.2.1/24'; then
  ip addr add 10.0.2.1/24 dev tap0 2>/dev/null || true
fi
ip link set tap0 up
ip addr show tap0 | grep -q '10.0.2.1/24' || {
  echo "ERROR: tap0 missing 10.0.2.1/24" >&2
  exit 1
}

# 4. Restart NFS server.
echo "[4/5] Restart NFS server"
systemctl restart nfs-kernel-server
ps aux | grep -q '[n]fsd' || {
  echo "ERROR: nfs-kernel-server restart failed" >&2
  exit 1
}

# 5. Verify.
echo "[5/5] Verify"
showmount -e localhost | grep -q "$EXPORT_DIR" || {
  echo "ERROR: export missing from showmount" >&2
  exit 1
}
ip link show tap0 | grep -q 'UP' || {
  echo "ERROR: tap0 is not UP" >&2
  exit 1
}

echo
echo "=== Setup Complete ==="
echo "QEMU guest network: ifconfig vtnet0 10.0.2.2 netmask 255.255.255.0 up"
echo "NFS mount:          fs-nfs3 10.0.2.1:$EXPORT_DIR /mnt/nfs"
