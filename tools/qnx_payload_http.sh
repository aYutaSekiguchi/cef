#!/usr/bin/env bash
# Build a payload.tar.gz for HTTP delivery into the QNX/QEMU rootless
# workflow. The runner spawns `python3 -m http.server` on a free port and
# the guest fetches the tarball via passt's host IP (default 192.168.0.1)
# then extracts it under /data/qnx_payload. This avoids the QEMU 2nd IDE
# drive mount problems we observed on this QNX 8 image (mkqnx6fsimg /
# ext2 / FAT all fail with `mount: Invalid argument` regardless of
# filesystem type or partition layout).
#
# Why this is the rootless default:
#   - Host: no sudo (passt runs as user, python3 http.server too)
#   - Guest: uses /data which is a 32 GB qnx6 partition (not the 18 MB
#     read-only root) so any reasonable payload fits.
#   - The runner wraps the call in a single shell command, so the
#     bash heredoc + `;` chain semantics stay simple.
#
# Usage:
#   qnx_payload_http.sh [options] --out TAR
#     --src DIR           Source directory the manifest refers to
#                         (default: $CHROMIUM_SRC/out/qnx_release)
#     --out  PATH         Output tar.gz path (required)
#     --payload-file PATH Extra file/dir to include at its basename in
#                         the tar. Repeatable. ERROR if missing.
#     --manifest NAME     Predefined payload set (currently: cefsimple)
#     --prefix PATH       Top-level directory inside the tar (default:
#                         payload). The guest extracts into
#                         /data/<prefix>, so the payload binary lives at
#                         /data/<prefix>/<basename>.
#     --fingerprint-file  Path for fingerprint cache (default: ${OUT}.fp)
#     --force             Rebuild even if fingerprint matches
#     --help
#
# Manifests: each emits host paths to include, one per line. New ones
# add a function and register in PAYLOAD_MANIFESTS below.
#
# Cache: SHA-256 over every UNIQUE path's mode + size + symlink target +
# full file content. The cache key is
# written next to the output; identical source fingerprints reuse the
# existing tarball without invoking tar.
#
# Exit codes:
#   0 success
#   1 missing prerequisites
#   2 usage error
#   3 build failure
#   4 explicit --payload-file not found

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHROMIUM_SRC_DEFAULT="$(cd "$SCRIPT_DIR/../.." && pwd)"

SRC="${CHROMIUM_SRC_DEFAULT}/out/qnx_release"
OUT=""
PREFIX="payload"
FINGERPRINT=""
FORCE=0
MANIFEST=""
declare -a EXTRA_PATHS=()

# ---------------------------------------------------------------------------
# Manifests: each emits host paths to include in the tar, one per line.
# Paths may be absolute or relative to --src.
#
# Only paths the guest actually needs at runtime are listed. dev-only
# tools (libcef_dll_wrapper for linking against CEF) are excluded.
# ---------------------------------------------------------------------------
manifest_cefsimple() {
  local src="$1"
  printf '%s\n' \
    "$src/cefsimple" \
    "$src/libcef.so" \
    "$src/libEGL.so" \
    "$src/libGLESv2.so" \
    "$src/chrome_crashpad_handler" \
    "$src/icudtl.dat" \
    "$src/v8_context_snapshot.bin" \
    "$src/locales" \
    "$src/resources.pak" \
    "$src/chrome_100_percent.pak" \
    "$src/chrome_200_percent.pak" \
    "$src/content_shell.pak" \
    "$src/cef_extensions.pak" \
    "$src/cef.pak" \
    "$src/cef_100_percent.pak" \
    "$src/cef_200_percent.pak" \
    "$src/cef_resources.pak" \
    "$src/ui_resources.pak" \
    "$src/ui_resources_100_percent.pak" \
    "$src/ui_resources_200_percent.pak"
}

manifest_common() {
  local src="$1"
  printf '%s\n' \
    "$src/icudtl.dat" \
    "$src/v8_context_snapshot.bin" \
    "$src/snapshot_blob.bin" \
    "$src/resources.pak"
}

declare -A PAYLOAD_MANIFESTS=(
  [cefsimple]=manifest_cefsimple
  [common]=manifest_common
)

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
usage() { sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --src) SRC="${2:?}"; shift 2 ;;
    --src=*) SRC="${1#*=}"; shift ;;
    --out) OUT="${2:?}"; shift 2 ;;
    --out=*) OUT="${1#*=}"; shift ;;
    --payload-file) EXTRA_PATHS+=("${2:?}"); shift 2 ;;
    --payload-file=*) EXTRA_PATHS+=("${1#*=}"); shift ;;
    --prefix) PREFIX="${2:?}"; shift 2 ;;
    --prefix=*) PREFIX="${1#*=}"; shift ;;
    --manifest) MANIFEST="${2:?}"; shift 2 ;;
    --manifest=*) MANIFEST="${1#*=}"; shift ;;
    --fingerprint-file) FINGERPRINT="${2:?}"; shift 2 ;;
    --fingerprint-file=*) FINGERPRINT="${1#*=}"; shift ;;
    --force) FORCE=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

[[ -n "$OUT" ]] || { echo "ERROR: --out is required" >&2; usage; exit 2; }
[[ -d "$SRC" ]] || { echo "ERROR: --src dir not found: $SRC" >&2; exit 2; }
[[ "$PREFIX" =~ ^[A-Za-z0-9._/-]+$ && "$PREFIX" != /* && "$PREFIX" != *..* ]] || {
  echo "ERROR: --prefix must be a safe relative path: $PREFIX" >&2; exit 2
}
[[ -z "$FINGERPRINT" ]] && FINGERPRINT="${OUT}.fp"

for tool in tar; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "ERROR: required tool not found: $tool" >&2; exit 1
  }
done

# ---------------------------------------------------------------------------
# Resolve payload paths: manifest + explicit --payload-file. Missing
# --payload-file entries are a usage error (not a warning).
# ---------------------------------------------------------------------------
declare -a RESOLVED=()
if [[ -n "$MANIFEST" ]]; then
  fn="${PAYLOAD_MANIFESTS[$MANIFEST]:-}"
  if [[ -z "$fn" ]]; then
    echo "ERROR: unknown manifest: $MANIFEST" >&2
    echo "       Available: ${!PAYLOAD_MANIFESTS[*]}" >&2
    exit 2
  fi
  while IFS= read -r p; do
    [[ -e "$p" ]] && RESOLVED+=("$p")
  done < <("$fn" "$SRC")
fi
MISSING_EXTRA=()
for p in "${EXTRA_PATHS[@]}"; do
  if [[ -e "$p" ]]; then
    RESOLVED+=("$p")
  else
    MISSING_EXTRA+=("$p")
  fi
done
if [[ ${#MISSING_EXTRA[@]} -gt 0 ]]; then
  echo "ERROR: --payload-file entries not found on host:" >&2
  for p in "${MISSING_EXTRA[@]}"; do
    echo "  $p" >&2
  done
  exit 4
fi

if [[ ${#RESOLVED[@]} -eq 0 ]]; then
  echo "ERROR: no payload paths resolved (--manifest / --payload-file mismatch?)" >&2
  exit 2
fi

# Deduplicate while preserving order.
declare -A seen=()
declare -A image_names=()
declare -a UNIQUE=()
for p in "${RESOLVED[@]}"; do
  if [[ -z "${seen[$p]:-}" ]]; then
    image_name="$(basename "$p")"
    if [[ -n "${image_names[$image_name]:-}" && "${image_names[$image_name]}" != "$p" ]]; then
      echo "ERROR: payload basename collision for '$image_name'" >&2
      exit 2
    fi
    seen[$p]=1
    image_names[$image_name]="$p"
    UNIQUE+=("$p")
  fi
done

# ---------------------------------------------------------------------------
# Fingerprint cache. For each UNIQUE path, record basename + stat
# (mode, mtime, size) + SHA-256 of full content. Symlinks record their
# target instead.
# ---------------------------------------------------------------------------
sha256_file() {
  sha256sum "$1" 2>/dev/null | awk '{print $1}'
}

compute_fingerprint() {
  {
    printf 'PREFIX|%s\n' "$PREFIX"
    for p in "${UNIQUE[@]}"; do
      if [[ -L "$p" ]]; then
        tgt=$(readlink "$p" || true)
        printf '%s|LINK|%s\n' "$p" "$tgt"
      elif [[ -d "$p" ]]; then
        (cd "$p" && find . -type f -o -type l | LC_ALL=C sort | while read -r f; do
           f_abs="$p/$f"
           st=$(stat -c '%a %Y %s' "$f_abs")
           if [[ -L "$f_abs" ]]; then
             tgt=$(readlink "$f_abs")
             printf '%s|%s|LINK|%s\n' "$f" "$st" "$tgt"
           else
             sha=$(sha256_file "$f_abs")
             printf '%s|%s|FILE|%s\n' "$f" "$st" "$sha"
           fi
         done)
      else
        st=$(stat -c '%a %Y %s' "$p")
        sha=$(sha256_file "$p")
        printf '%s|%s|FILE|%s\n' "$p" "$st" "$sha"
      fi
    done
  } | sha256sum | awk '{print $1}'
}

NEW_FP="$(compute_fingerprint)"
if [[ $FORCE -eq 0 && -f "$OUT" && -f "$FINGERPRINT" && "$(cat "$FINGERPRINT" 2>/dev/null)" == "$NEW_FP" ]]; then
  echo "=== QNX payload cache hit ==="
  echo "Output : $OUT"
  echo "SHA-256: $NEW_FP"
  ls -la "$OUT"
  exit 0
fi

echo "=== QNX payload tar build ==="
echo "Source : $SRC"
echo "Output : $OUT"
echo "Prefix : $PREFIX"
echo "Files  : ${#UNIQUE[@]}"
echo "New FP : $NEW_FP"

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"

# Build the tar. -C each UNIQUE so we can use --transform to lay each
# path at <prefix>/<basename>. Tar's --transform rewriter handles the
# "p" prefix->"prefix/p" path mapping cleanly. We pass paths one by
# one to keep the buildfile simple and to ensure each path appears at
# the expected location even when the source paths span multiple
# directories.
TAR_ARGS=(
  --transform="s,^,${PREFIX}/,"
)
for p in "${UNIQUE[@]}"; do
  TAR_ARGS+=(-C "$(dirname "$p")" "$(basename "$p")")
done

echo "--- running tar ---"
tar -czf "$OUT" "${TAR_ARGS[@]}" || {
  rc=$?
  echo "ERROR: tar failed (rc=$rc)" >&2
  exit 3
}

echo "$NEW_FP" >"$FINGERPRINT"

echo "=== done ==="
ls -la "$OUT"
echo "Contents (first 20 entries):"
tar -tzf "$OUT" | head -20 || true
