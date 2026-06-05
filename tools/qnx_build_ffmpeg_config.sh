#!/bin/bash
# Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be found
# in the LICENSE file.
#
# Regenerate the third_party/ffmpeg/chromium/config/Chromium/qnx/<arch>/
# platform configs that Chromium's GN build consumes for QNX.
#
# Rationale: Chromium's GN build does not run FFmpeg's `configure`; it reads
# pre-generated config.{h,asm}, config_components.{h,asm}, libavutil/avconfig.h,
# libavutil/ffversion.h, and libav{format,codec}/{demuxer,muxer,protocol,bsf,
# codec,parser}_list.c from a per-platform config directory. For QNX, those
# 12 files were originally copied byte-for-byte from
# third_party/ffmpeg/chromium/config/Chromium/linux/<arch>/ (see
# docs/qnx/fixes-and-decisions.md entry #33), but that shortcut leaves
# stale data in config.h (OS_NAME=linux, FFMPEG_CONFIGURATION string,
# FFMPEG_DATADIR paths) and libavutil/ffversion.h (older FFmpeg commit).
# This script drives FFmpeg's `configure` natively for QNX so the 12 files
# are the correct ones for the QNX target, the current FFmpeg commit, and
# the selected ffmpeg_branding.
#
# Output: <output-dir>/Chromium/qnx/<arch>/... (mirrors
# third_party/ffmpeg/chromium/config/Chromium/qnx/<arch>/).
#
# Note: this script does NOT regenerate ffmpeg_generated.gni (the GN
# source list). The existing ffmpeg_generated.gni routes QNX through
# `use_linux_config = is_linux || is_chromeos || is_fuchsia` (and
# BUILDCONFIG.gn sets `is_linux = current_os == "linux" || is_qnx`), with
# the Chrome-only sources gated on `ffmpeg_branding == "Chrome"`. For the
# same (target arch, ffmpeg_branding, enabled components), the FFmpeg
# `configure`-derived OBJS list is identical between Linux and QNX — only
# the target OS name and a few paths differ. We verified that
# empirically by running `make -n` for the QNX configure and diffing
# against ffmpeg_generated.gni's effective source list; the only
# differences are 23 files that generate_gn.py's CleanObjectFiles
# intentionally removes (binary-size / link-warning hygiene), and 168+
# files that are gated on `ffmpeg_branding == "Chrome"`. So no
# ffmpeg_generated.gni regeneration is needed when the user toggles
# ffmpeg_branding on QNX — the file already supports both Chromium and
# Chrome branding for the QNX target.
#
# See docs/qnx/fixes-and-decisions.md entry #45 for the full rationale,
# including the cross-check against the existing Chromium/linux/<arch>/
# configs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHROMIUM_SRC="${CHROMIUM_SRC:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
FFMPEG_SRC="$CHROMIUM_SRC/third_party/ffmpeg"
QNX_SDP_ROOT="${QNX_SDP_ROOT:-/home/yuta/qnx800}"
QNX_HOST_DEFAULT="$QNX_SDP_ROOT/host/linux/x86_64"
QNX_TARGET_DEFAULT="$QNX_SDP_ROOT/target/qnx"
CHROMIUM_CLANG="$CHROMIUM_SRC/third_party/llvm-build/Release+Asserts/bin"

# Defaults. Override with --arch / --branding / --output-dir.
ARCH="x64"
BRANDING="chromium"   # chromium | chrome
OUTPUT_DIR=""         # auto-set below if empty
LOG_PREFIX="/tmp/qnx_ffmpeg_build"

usage() {
  cat <<EOF
Usage: $0 [--arch x64|arm64] [--branding chromium|chrome] [--output-dir DIR]

Regenerates the 12-file FFmpeg Chromium-style QNX platform config tree.

Options:
  --arch ARCH          Target CPU architecture (x64, arm64). Default: x64.
  --branding BRAND     FFmpeg branding: chromium or chrome. Default: chromium.
                       (chrome additionally enables aac/h264 decoders +
                       demuxers + parsers, matching build_ffmpeg.py.)
  --output-dir DIR     Where to write the regenerated tree. Default:
                       <cef>/patch/qnx/chromium/new_files/third_party/ffmpeg/
                       chromium/config/<Branding>/qnx/<arch>/
  --qnx-sdp-root DIR   QNX SDP root. Default: /home/yuta/qnx800
                       (overridable via env QNX_SDP_ROOT).
  -h, --help           Show this help.

Environment:
  CHROMIUM_SRC         Chromium src root. Auto-detected from script location.
  QNX_SDP_ROOT         Same as --qnx-sdp-root.

The script does NOT modify any tracked Chromium file in-place; it only
writes into the output directory. The caller is expected to commit the
resulting tree as CEF new_files.

Examples:
  # Regenerate the QNX x64 Chromium branding config tree in-place.
  $0

  # Regenerate the QNX x64 Chrome branding config tree into a temp dir.
  $0 --branding chrome --output-dir /tmp/qnx_ffmpeg_chrome_x64

  # Generate for QNX arm64.
  $0 --arch arm64
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --arch)         ARCH="$2"; shift 2 ;;
    --branding)     BRANDING="$2"; shift 2 ;;
    --output-dir)   OUTPUT_DIR="$2"; shift 2 ;;
    --qnx-sdp-root) QNX_SDP_ROOT="$2"; QNX_HOST_DEFAULT="$2/host/linux/x86_64"; QNX_TARGET_DEFAULT="$2/target/qnx"; shift 2 ;;
    -h|--help)      usage; exit 0 ;;
    *)              echo "ERROR: unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

case "$ARCH" in
  x64)   TARGET_TRIPLE="x86_64-unknown-nto"; CROSS_PREFIX="x86_64-pc-nto-qnx8.0.0-"; GCC_VARIANT="gcc_ntox86_64_cxx"; ARCH_FFMPEG="x86_64"; AR_BIN="ntox86_64-ar"; NM_BIN="ntox86_64-nm" ;;
  arm64) TARGET_TRIPLE="aarch64-unknown-nto"; CROSS_PREFIX="aarch64-unknown-nto-qnx8.0.0-"; GCC_VARIANT="gcc_ntoaarch64_cxx"; ARCH_FFMPEG="aarch64"; AR_BIN="ntoaarch64-ar"; NM_BIN="ntoaarch64-nm" ;;
  *)     echo "ERROR: --arch must be x64 or arm64 (got: $ARCH)" >&2; exit 1 ;;
esac

case "$BRANDING" in
  chromium) BRANDING_CAP="Chromium" ;;
  chrome)   BRANDING_CAP="Chrome" ;;
  *)        echo "ERROR: --branding must be chromium or chrome (got: $BRANDING)" >&2; exit 1 ;;
esac

if [[ -z "$OUTPUT_DIR" ]]; then
  OUTPUT_DIR="$SCRIPT_DIR/../patch/qnx/chromium/new_files/third_party/ffmpeg/chromium/config/${BRANDING_CAP}/qnx/${ARCH}"
fi
OUTPUT_DIR="$(mkdir -p "$OUTPUT_DIR" && cd "$OUTPUT_DIR" && pwd)"

export QNX_HOST="${QNX_HOST:-$QNX_HOST_DEFAULT}"
export QNX_TARGET="${QNX_TARGET:-$QNX_TARGET_DEFAULT}"

if [[ ! -d "$FFMPEG_SRC" ]]; then
  echo "ERROR: FFmpeg source tree not found: $FFMPEG_SRC" >&2
  exit 1
fi
if [[ ! -x "$QNX_HOST/usr/bin/qcc" ]]; then
  echo "ERROR: QNX QCC compiler not found: $QNX_HOST/usr/bin/qcc" >&2
  exit 1
fi
if [[ ! -d "$QNX_TARGET" ]]; then
  echo "ERROR: QNX sysroot not found: $QNX_TARGET" >&2
  exit 1
fi
if [[ ! -x "$CHROMIUM_CLANG/clang" ]]; then
  echo "ERROR: Chromium bundled clang not found: $CHROMIUM_CLANG/clang" >&2
  exit 1
fi
if ! command -v nasm >/dev/null 2>&1; then
  echo "ERROR: nasm not found on PATH (install nasm for x64 ASM support)." >&2
  exit 1
fi

BUILD_ROOT="${LOG_PREFIX}_${BRANDING}_${ARCH}"
LOGFILE="$BUILD_ROOT/configure.log"
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"

echo "============================================================"
echo "QNX FFmpeg config generation"
echo "  Branding:           $BRANDING ($BRANDING_CAP)"
echo "  Arch:               $ARCH"
echo "  QNX SDP root:       $QNX_SDP_ROOT"
echo "  QNX host:           $QNX_HOST"
echo "  QNX target:         $QNX_TARGET"
echo "  Chromium src:       $CHROMIUM_SRC"
echo "  Output dir:         $OUTPUT_DIR"
echo "  Build dir:          $BUILD_ROOT"
echo "============================================================"

# Build the configure command. Mirrors the flag set used by
# media/ffmpeg/scripts/build_ffmpeg.py for the linux/x64 (Chromium brand)
# target, with QNX-specific cross-compile flags swapped in. The
# `--disable-asm` switch is intentionally OMITTED so that `config.asm` and
# `config_components.asm` are produced (the ffmpeg_nasm target in
# third_party/ffmpeg/BUILD.gn lists them as inputs even when no NASM
# source files are compiled for a given platform).
case "$ARCH" in
  x64)   ARCH_DEFINE="-D__X86_64__" ;;
  arm64) ARCH_DEFINE="-D__AARCH64__" ;;
esac

COMMON_FLAGS=(
  --target-os=qnx
  --enable-cross-compile
  "--cross-prefix=$QNX_HOST/usr/bin/$CROSS_PREFIX"
  "--cc=$CHROMIUM_CLANG/clang"
  "--cxx=$CHROMIUM_CLANG/clang++"
  "--ld=$QNX_HOST/usr/bin/qcc"
  "--ar=$QNX_HOST/usr/bin/$AR_BIN"
  "--nm=$QNX_HOST/usr/bin/$NM_BIN"
  --enable-pic
  "--arch=$ARCH_FFMPEG"
  "--extra-cflags=--target=$TARGET_TRIPLE -D__QNXNTO__ -D__QNX__ -D__LITTLEENDIAN__ -D__EXT_XOPEN_EX -D_QNX_SOURCE -D_POSIX_C_SOURCE=200809L $ARCH_DEFINE -I$CHROMIUM_SRC/third_party/opus/src/include"
  "--extra-ldflags=-V$GCC_VARIANT"
  --extra-libs=-lsocket
  --disable-everything --disable-all
  --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages
  --disable-static
  --enable-avcodec --enable-avformat --enable-avutil --enable-static
  --enable-libopus
  --disable-debug --disable-bzlib --disable-error-resilience --disable-iconv
  --disable-network --disable-schannel --disable-sdl2 --disable-symver
  --disable-xlib --disable-zlib --disable-securetransport --disable-faan
  --disable-alsa --disable-iamf --disable-autodetect
  --enable-pic
  --disable-linux-perf
  --x86asmexe=nasm
  --optflags=-O2
  --enable-decoder=vorbis,libopus,flac
  '--enable-decoder=pcm_u8,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,mp3'
  '--enable-decoder=pcm_s16be,pcm_s24be,pcm_mulaw,pcm_alaw'
  --enable-demuxer=ogg,matroska,wav,flac,mp3,mov
  --enable-parser=opus,vorbis,flac,mpegaudio,vp9
)

CHROME_FLAGS=(
  --enable-decoder=aac,h264
  --enable-demuxer=aac
  --enable-parser=aac,h264
)

EXTRA_BRAND_FLAGS=()
if [[ "$BRANDING" == "chrome" ]]; then
  EXTRA_BRAND_FLAGS=("${CHROME_FLAGS[@]}")
fi

pushd "$BUILD_ROOT" >/dev/null
echo "Running configure..."
"$FFMPEG_SRC/configure" \
  "${COMMON_FLAGS[@]}" \
  "${EXTRA_BRAND_FLAGS[@]}" \
  "--logfile=$LOGFILE" \
  "--prefix=$BUILD_ROOT/install" 2>&1 | tail -8 || {
    echo "ERROR: configure failed. See $LOGFILE" >&2
    popd >/dev/null
    exit 1
  }

# `make` runs the configure-generated list-file and version targets
# without trying to compile the whole tree. `make libavutil/ffversion.h`
# runs the configure step AND the version.sh script.
echo "Running make to generate *-list.c and ffversion.h..."
make libavutil/ffversion.h 2>&1 | tail -5

# Run version.sh explicitly to be safe (make may not run it under all
# invocation orders; the version.sh call is idempotent).
(cd "$FFMPEG_SRC" && sh ffbuild/version.sh . "$BUILD_ROOT/libavutil/ffversion.h") \
  || { echo "ERROR: version.sh failed" >&2; popd >/dev/null; exit 1; }

popd >/dev/null

# Copy the 12 expected config files into the output directory.
echo "Copying 12 config files to: $OUTPUT_DIR"
for f in config.asm config.h config_components.asm config_components.h \
         libavutil/avconfig.h libavutil/ffversion.h \
         libavcodec/bsf_list.c libavcodec/codec_list.c libavcodec/parser_list.c \
         libavformat/demuxer_list.c libavformat/muxer_list.c libavformat/protocol_list.c; do
  mkdir -p "$OUTPUT_DIR/$(dirname "$f")"
  cp -f "$BUILD_ROOT/$f" "$OUTPUT_DIR/$f"
done

echo
echo "============================================================"
echo "Done. Generated files:"
ls -la "$OUTPUT_DIR" "$OUTPUT_DIR/libavutil" "$OUTPUT_DIR/libavcodec" "$OUTPUT_DIR/libavformat"
echo
echo "Review the diff vs the previous tree with:"
echo "  diff -ruN <prev-tree> $OUTPUT_DIR"
echo
echo "Commit via the CEF patch system as:"
echo "  cef/patch/qnx/chromium/new_files/third_party/ffmpeg/chromium/config/${BRANDING_CAP}/qnx/${ARCH}/"
echo "============================================================"
