#!/bin/bash
# Copyright (c) 2025 The Chromium Embedded Framework Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be found
# in the LICENSE file.
#
# This script creates QNX-specific CEF project files by:
# 1. Installing QNX-specific new files from cef/patch/qnx/chromium/new_files/
# 2. Applying QNX-specific patches from cef/patch/patches/qnx/
# 3. Setting up QNX-specific GN arguments
# 4. Running gn gen with QNX toolchain

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CEF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CHROMIUM_SRC_DIR="$(cd "${CEF_DIR}/.." && pwd)"

# Default values
BUILD_TYPE="Release"
QNX_SDP_ROOT="${QNX_SDP_ROOT:-$HOME/qnx800}"
QNX_TARGET="${QNX_TARGET:-${QNX_SDP_ROOT}/target/qnx}"
QNX_HOST="${QNX_HOST:-${QNX_SDP_ROOT}/host/linux/x86_64}"

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-type)
      BUILD_TYPE="$2"
      shift 2
      ;;
    --qnx-sdp-root)
      QNX_SDP_ROOT="$2"
      shift 2
      ;;
    --qnx-target)
      QNX_TARGET="$2"
      shift 2
      ;;
    --qnx-host)
      QNX_HOST="$2"
      shift 2
      ;;
    --help)
      echo "Usage: $0 [options]"
      echo ""
      echo "Options:"
      echo "  --build-type <type>     Build type: Debug or Release (default: Release)"
      echo "  --qnx-sdp-root <path>   QNX SDP installation root (default: ~/qnx800)"
      echo "  --qnx-target <path>     QNX target sysroot (default: <sdp>/target/qnx)"
      echo "  --qnx-host <path>       QNX host tools dir (default: <sdp>/host/linux/x86_64)"
      echo "  --help                  Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

# Validate QNX SDP installation
if [[ ! -d "${QNX_SDP_ROOT}/target/qnx" ]]; then
  echo "ERROR: QNX SDP not found at ${QNX_SDP_ROOT}"
  echo "Please set QNX_SDP_ROOT environment variable or use --qnx-sdp-root"
  exit 1
fi
if [[ ! -d "${QNX_TARGET}" ]]; then
  echo "ERROR: QNX target sysroot not found at ${QNX_TARGET}"
  exit 1
fi
if [[ ! -d "${QNX_HOST}" ]]; then
  echo "ERROR: QNX host tools not found at ${QNX_HOST}"
  exit 1
fi

# Initialize submodules if needed.
echo "Checking submodules..."
cd "${CHROMIUM_SRC_DIR}"
if [[ ! -f "third_party/googletest/src/googletest/src/gtest-death-test.cc" ]]; then
  echo "  Initializing googletest submodule..."
  git submodule update --init third_party/googletest/src || true
fi
if [[ ! -f "third_party/perfetto/src/base/test/vm_test_utils.cc" ]]; then
  echo "  Initializing perfetto submodule..."
  git submodule update --init third_party/perfetto || true
fi
if [[ ! -f "third_party/boringssl/src/crypto/rand/internal.h" ]]; then
  echo "  Initializing boringssl submodule..."
  git submodule update --init third_party/boringssl/src || true
fi
if [[ ! -f "third_party/ced/src/util/basictypes.h" ]]; then
  echo "  Initializing ced submodule..."
  git submodule update --init third_party/ced/src || true
fi
cd "${CEF_DIR}"

echo "CEF QNX Project Creator"
echo "======================="
echo "Build type: ${BUILD_TYPE}"
echo "QNX SDP: ${QNX_SDP_ROOT}"
echo "QNX Target: ${QNX_TARGET}"
echo "QNX Host: ${QNX_HOST}"
echo ""

# Step 1: Install new QNX platform files.
echo "Step 1: Installing new QNX platform files..."
NEW_FILES_DIR="${CEF_DIR}/patch/qnx/chromium/new_files"
if [[ -d "${NEW_FILES_DIR}" ]]; then
  while IFS= read -r -d '' file; do
    rel_path="${file#${NEW_FILES_DIR}/}"
    target_file="${CHROMIUM_SRC_DIR}/${rel_path}"
    mkdir -p "$(dirname "${target_file}")"
    if [[ ! -f "${target_file}" ]]; then
      echo "  Installing: ${rel_path}"
      cp "${file}" "${target_file}"
    elif ! cmp -s "${file}" "${target_file}"; then
      echo "  Updating:   ${rel_path}"
      cp "${file}" "${target_file}"
    else
      echo "  Keeping:    ${rel_path}"
    fi
  done < <(find "${NEW_FILES_DIR}" -type f -print0 | sort -z)
fi
echo ""

# Step 2: Apply QNX-specific patches.
echo "Step 2: Applying QNX-specific patches..."
PYTHON3="${PYTHON3:-python3}"

# Submodule patches must be applied from the submodule root with submodule-
# relative paths.
"${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file qnx/googletest_death_test --patch-dir third_party/googletest/src
"${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file qnx/perfetto_aggregate_init --patch-dir third_party/perfetto
"${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file qnx/perfetto_mincore --patch-dir third_party/perfetto
"${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file qnx/perfetto_unix_socket --patch-dir third_party/perfetto
"${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file qnx/boringssl_qnx_support --patch-dir third_party/boringssl/src
"${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file qnx/ced_qnx_basictypes --patch-dir third_party/ced/src

# Apply additional Chromium QNX patches.
if [[ -d "${CEF_DIR}/patch/patches/qnx/chromium" ]]; then
  echo "Applying Chromium QNX patches..."
  for patch_file in "${CEF_DIR}/patch/patches/qnx/chromium"/*.patch; do
    if [[ -f "${patch_file}" ]]; then
      patch_name="$(basename "${patch_file}" .patch)"
      echo "  - ${patch_name}"
      "${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file "qnx/chromium/${patch_name}"
    fi
  done
fi

# Some QNX support files introduce new submodule paths not present at the
# compatibility tag. Initialize them after patch application.
cd "${CHROMIUM_SRC_DIR}"
if [[ -f ".gitmodules" ]] && grep -q 'third_party/epoll/src' .gitmodules; then
  if [[ ! -e "third_party/epoll/src/epoll.c" ]]; then
    echo "  Initializing epoll submodule..."
    git submodule update --init third_party/epoll/src || true
  fi
  if [[ -e "third_party/epoll/src/epoll.c" ]]; then
    echo "  Applying epoll QNX patch..."
    "${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file qnx/epoll_sigevent_qnx --patch-dir third_party/epoll/src
  fi
fi
cd "${CEF_DIR}"

echo ""

# Step 3: Set up build directory.
BUILD_DIR="${CHROMIUM_SRC_DIR}/out/qnx_${BUILD_TYPE,,}"
mkdir -p "${BUILD_DIR}"

echo "Step 3: Build directory: ${BUILD_DIR}"
echo ""

# Step 4: Create GN args file.
echo "Step 4: Creating GN args..."

GN_ARGS_FILE="${BUILD_DIR}/args.gn"
cat > "${GN_ARGS_FILE}" << EOF
# QNX-specific GN args for CEF
# Auto-generated by cef_create_projects_qnx.sh

target_os = "qnx"
target_cpu = "x64"

# Build type
is_debug = $([[ "${BUILD_TYPE}" == "Debug" ]] && echo "true" || echo "false")
is_component_build = false
is_official_build = $([[ "${BUILD_TYPE}" == "Release" ]] && echo "true" || echo "false")

# QNX baseline must avoid ThinLTO. Do not force use_lld=false here because
# global use_lld overrides can break host tool builds.
use_thin_lto = false
thin_lto_enable_optimizations = false

# QNX non-component builds should avoid symbol_level=2 unless using debug
# fission. Keep symbols lightweight and compatible.
symbol_level = 1
blink_symbol_level = 0
v8_symbol_level = 0

# QNX toolchain
qnx_sdp_root = "${QNX_SDP_ROOT}"

# Disable features not supported on QNX
enable_print_preview = false
enable_printing = false
enable_widevine = false
enable_nacl = false
enable_mdns = false
enable_remoting = false

# CEF specific
cef_target_arch = "x64"
cef_use_alloc_shim = false
use_crash_key_stubs = true

# Compiler settings (QNX uses Clang for compile, QCC for link)
is_clang = true
clang_use_chrome_plugins = false
use_autogenerated_modules = false
use_clang_modules = false
treat_warnings_as_errors = false

# Disable sandbox (not supported on QNX)
cef_enable_sandbox = false
v8_enable_sandbox = false

# Test/settings overrides
enable_base_tracing = false
use_custom_libcxx = false
use_custom_libcxx_for_host = true
chrome_pgo_phase = 0

# QNX-specific overrides
use_qt = false
use_qt5 = false
use_qt6 = false
use_ozone = true
use_x11 = false
use_glib = false
use_gio = false
use_gtk = false
use_dbus = false
use_pangocairo = false
use_xkbcommon = false
use_nss_certs = false
use_udev = false
use_system_minigbm = false
use_system_libffi = false
ozone_platform_wayland = false
ozone_platform_x11 = false
ozone_platform_drm = false
rtc_use_pipewire = false
use_vaapi = false
EOF

echo "GN args written to: ${GN_ARGS_FILE}"
echo ""

# Step 5: Run gn gen.
echo "Step 5: Running gn gen..."
cd "${CHROMIUM_SRC_DIR}"

# Set up environment for gn.
export QNX_SDP_ROOT
export QNX_TARGET
export QNX_HOST

# Run gn gen. args.gn is already written above, so do not inline it via
# --args=... because collapsing newlines would make '#' comments comment out
# the rest of the file.
gn gen "${BUILD_DIR}"

# Write helper scripts so build-time tools inherit the same QNX SDK env.
cat > "${BUILD_DIR}/qnx_env.sh" << EOF
export QNX_SDP_ROOT="${QNX_SDP_ROOT}"
export QNX_TARGET="${QNX_TARGET}"
export QNX_HOST="${QNX_HOST}"
EOF

cat > "${BUILD_DIR}/ninja_qnx.sh" << EOF
#!/bin/bash
set -e
SCRIPT_DIR="\$(cd "\$(dirname "\$0")" && pwd)"
source "\${SCRIPT_DIR}/qnx_env.sh"
cd "${CHROMIUM_SRC_DIR}"
exec ninja -C "${BUILD_DIR}" "\$@"
EOF
chmod +x "${BUILD_DIR}/ninja_qnx.sh"

echo ""
echo "Success! QNX CEF project files created."
echo ""
echo "Build directory: ${BUILD_DIR}"
echo ""
echo "To build:"
echo "  ${BUILD_DIR}/ninja_qnx.sh cef"
echo ""
echo "Or in your current shell:"
echo "  source ${BUILD_DIR}/qnx_env.sh"
echo "  ninja -C ${BUILD_DIR} cef"
echo ""
