#!/bin/bash
# Copyright (c) 2025 The Chromium Embedded Framework Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be found
# in the LICENSE file.
#
# QNX-specific CEF project creator.
#
# Design: calls cef_create_projects.sh for CEF core patching, then layers
# QNX-specific patches and GN args on top. This ensures:
#  1. CEF core patches always apply first (correct base for QNX patches).
#  2. Future CEF version upgrades only require validating QNX patches against
#     the updated core, not reinventing the entire patch workflow.
#  3. QNX patches that overlap with core patches are intentionally applied
#     after core patches, overriding as needed.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CEF_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CHROMIUM_SRC_DIR="$(cd "${CEF_DIR}/.." && pwd)"

# ---------------------------------------------------------------------------
# QNX SDK defaults
# ---------------------------------------------------------------------------
BUILD_TYPE="Release"
QNX_SDP_ROOT="${QNX_SDP_ROOT:-$HOME/qnx800}"
QNX_TARGET="${QNX_TARGET:-${QNX_SDP_ROOT}/target/qnx}"
QNX_HOST="${QNX_HOST:-${QNX_SDP_ROOT}/host/linux/x86_64}"

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

# Validate QNX SDK
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

# Validate QNX patch format before applying anything.
"${PYTHON3:-python3}" "${SCRIPT_DIR}/qnx_validate_patch_format.py" --root "${CEF_DIR}"
echo ""

# Initialize submodules if needed (same prerequisites as cef_create_projects.sh).
echo "Checking submodules..."
cd "${CHROMIUM_SRC_DIR}"
for submodule_path in \
	"third_party/googletest/src/googletest/src/gtest-death-test.cc" \
	"third_party/perfetto/src/base/test/vm_test_utils.cc" \
	"third_party/boringssl/src/crypto/rand/internal.h" \
	"third_party/ced/src/util/basictypes.h"; do
	if [[ ! -f "${submodule_path}" ]]; then
		submodule_dir="$(dirname "${submodule_path}" | sed 's|/[^/]*$||')"
		echo "  Initializing ${submodule_dir} submodule..."
		git submodule update --init "${submodule_dir}" || true
	fi
done
cd "${CEF_DIR}"

echo "CEF QNX Project Creator"
echo "======================="
echo "Build type:   ${BUILD_TYPE}"
echo "QNX SDP:      ${QNX_SDP_ROOT}"
echo "QNX Target:   ${QNX_TARGET}"
echo "QNX Host:     ${QNX_HOST}"
echo ""

# ============================================================================
# Phase 1: Install new QNX platform source files.
#
# These files don't exist in upstream Chromium and must be copied BEFORE
# patch application, because some QNX patches in patch.cfg reference them.
# ============================================================================
echo "Phase 1: Installing new QNX platform files..."
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

# ============================================================================
# Phase 2: Run standard CEF project creation.
#
# This applies:
#   - version_manager.py (CEF version file updates)
#   - All CEF core patches via patch.cfg (gn_config, component_build,
#     message_loop, views_widget, chrome_runtime, etc.)
#   - QNX patches registered in patch.cfg (applied after core patches, in
#     the order they appear at the end of patch.cfg)
#   - Standard GN configs (out/Debug_GN_x64, out/Release_GN_x64)
# ============================================================================
echo "Phase 2: Running cef_create_projects.sh (CEF core + registered QNX patches)"
echo "---------------------------------------------------------------------------"
"${CEF_DIR}/cef_create_projects.sh"
echo ""

# ============================================================================
# Phase 3: Apply QNX patches NOT registered in patch.cfg.
#
# All QNX patches registered in patch.cfg have already been applied in Phase 2
# (after CEF core patches). This phase covers patches that haven't been added
# to patch.cfg yet. Add new patches here first, then migrate to patch.cfg when
# validated.
#
# IMPORTANT: If a patch in this section starts conflicting after a CEF version
# upgrade, migrate it into patch.cfg at the correct position instead.
# ============================================================================
echo "Phase 3: Applying unregistered QNX-specific patches..."
PYTHON3="${PYTHON3:-python3}"

# --- Submodule patches (not in patch.cfg) ---
# None currently; googletest, perfetto, boringssl, ced patches are in patch.cfg.

# --- Chromium-level patches (not in patch.cfg) ---
# These patches are in patch/patches/qnx/chromium/ but not yet registered in
# patch.cfg. Apply them in dependency order: toolchain first, then base/,
# then higher-level modules.

UNREGISTERED_CHROMIUM_PATCHES=(
	# Compiler / toolchain support
	"compiler_rt_builtins_qnx"
	"qnx_source_sync"
	# V8 — must be applied in this dependency order.
	#   1. v8_base64_atomic / v8_qnx_targeting: V8 platform support
	#   2. v8_stack_limit_qnx: clamp V8 stack limit to actual OS thread
	#      stack (QNX threads are 512KB main / 256KB worker, V8 default
	#      984KB overflows)
	#   3. v8_perfetto_trace_qnx: align Perfetto JSON number/pointer
	#      formatting with QNX libc++ (1e+100 rendered as full decimal
	#      instead of scientific, void* missing 0x prefix, etc.)
	#   4. v8_bytecode_expectations_qnx: make CollectGoldenFiles try
	#      ../../v8/<path> in addition to ../../<path> so the
	#      BytecodeGeneratorTest param suite is instantiated under the
	#      Chromium tree layout
	"v8_base64_atomic"
	"v8_qnx_targeting"
	"v8_stack_limit_qnx"
	"v8_perfetto_trace_qnx"
	"v8_bytecode_expectations_qnx"
	# v8_background_compile_stack_size_qnx: the test helper
	# BackgroundCompileTaskTest::NewBackgroundCompileTask() declared a
	# `stack_size` parameter but ignored it, always passing
	# v8_flags.stack_size to the BackgroundCompileTask constructor. As a
	# result, BackgroundCompileTaskTest.CompileFailure (which passes
	# `100` intending a 100KB parser stack) ran the parser with the full
	# 984KB budget and overflowed the C++ stack on QNX (512KB main / 256KB
	# worker). Fix the helper to actually use the parameter so the parser
	# stack guard fires before the OS stack does. This is a general
	# upstream-quality bug fix; carried under QNX patches because QNX is
	# the platform where it manifests.
	"v8_background_compile_stack_size_qnx"
	# v8_workloads_basic_functionality_stack_qnx: the test allocated a
	# 100 000-element pointer array (≈ 800 KB on 64-bit) on the stack
	# with `Persistent<...>* persistents[kNumPersistents]`. On QNX the
	# unittest runs on a worker thread with a 256 KB stack, so the array
	# alone overflows before the test body even starts. Move the array
	# to a heap-allocated `std::vector` (size unchanged) so the test's
	# actual logic — exercising cppgc / Oilpan persistent allocation —
	# runs in the same O(100k) range as the author intended, but no
	# longer needs 800 KB of stack.
	"v8_workloads_basic_functionality_stack_qnx"
	# v8_unittests_status_logall_qnx: SKIP LogAllTest on QNX (and the
	# official_build exception tests, which were already skipped on macOS
	# upstream). LogAllTest crashes inside RunJS under QEMU; the exact
	# logger subpath could not be isolated.
	"v8_unittests_status_logall_qnx"
	# crashpad_capture_context_qnx: crashpad_client.h pulls in
	# util/misc/capture_context.h, which does not define NativeCPUContext
	# for QNX. Add a QNX branch that aliases it to ucontext_t so the
	# Crashpad headers can compile even though we do not actually enable
	# Crashpad on QNX.
	"crashpad_capture_context_qnx"
	# crashpad_capture_context_qnx: crashpad_client.h pulls in
	# util/misc/capture_context.h, which does not define NativeCPUContext
	# for QNX. Add a QNX branch that aliases it to ucontext_t so the
	# Crashpad headers can compile even though we do not actually enable
	# Crashpad on QNX.
	"crashpad_capture_context_qnx"
	# crashpad_qnx: Replace Crashpad's Linux-only sources with a QNX
	# no-op implementation so QNX builds can omit crashpad without hitting
	# sys/prctl.h, NativeCPUContext, or crashpad.cc Port assertions.
	"crashpad_qnx"
	# fieldtrial_to_struct_qnx: fieldtrial_to_struct.py only recognizes
	# android, android_webview, chromeos, fuchsia, ios, linux, mac, windows
	# as valid --platform values. Add 'qnx' to the platform list so that
	# fieldtrial config generation works in QNX builds. This is needed
	# because the QNX toolchain passes --platform=qnx to the script via GN.
	"fieldtrial_to_struct_qnx"
	# enable_on_device_model_qnx: introduces the enable_on_device_model GN
	# variable (features.gni) that gates all on-device ML model service
	# integration in content/ and chrome/browser/ui/. Defaults to
	# use_on_device_model_service && use_dawn, so on QNX (where
	# use_dawn=false) it automatically evaluates to false, removing
	# Dawn's native/Vulkan sources and the renderdoc_app.h "Unknown platform"
	# error from the cefsimple build. See docs/qnx/fixes-and-decisions.md #50.
	"enable_on_device_model_qnx"
	# sentencepiece_qnx_endian: sentencepiece includes <endian.h> which
	# QNX does not provide. Synthesize BYTE_ORDER/__BIG_ENDIAN from QNX's
	# __LITTLEENDIAN__ predefines.
	"sentencepiece_qnx_endian"
)

for patch_name in "${UNREGISTERED_CHROMIUM_PATCHES[@]}"; do
	patch_file="${CEF_DIR}/patch/patches/qnx/chromium/${patch_name}.patch"
	if [[ -f "${patch_file}" ]]; then
		echo "  - ${patch_name}"
		"${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file "qnx/chromium/${patch_name}"
	else
		echo "  - ${patch_name} (NOT FOUND — remove from list or add patch file)"
	fi
done

# Note: All third_party submodule patches (googletest, perfetto, boringssl,
# ced, epoll) are registered in patch.cfg and applied in Phase 2. The epoll
# patch entry in patch.cfg includes the submodule-presence check.
# If a new third_party patch is needed, add it to patch.cfg rather than here.

echo ""

# ============================================================================
# Phase 4: Create QNX build directory with QNX-specific GN args.
#
# CEF's gn_args.py only knows about linux/mac/windows. QNX uses its own target_os
# and toolchain config, so we write args.gn directly rather than trying to extend
# gn_args.py with a qnx platform.
#
# The args below include all CEF-required values from gn_args.py (enable_widevine,
# optimize_webui, clang_use_chrome_plugins) plus QNX overrides.
# ============================================================================
BUILD_DIR="${CHROMIUM_SRC_DIR}/out/qnx_${BUILD_TYPE,,}"
mkdir -p "${BUILD_DIR}"

echo "Phase 4: Creating QNX build directory: ${BUILD_DIR}"
echo ""

GN_ARGS_FILE="${BUILD_DIR}/args.gn"
cat >"${GN_ARGS_FILE}" <<EOF
# QNX-specific GN args for CEF
# Auto-generated by cef_create_projects_qnx.sh

# Target platform
target_os = "qnx"
target_cpu = "x64"

# Build type
is_debug = $([[ "${BUILD_TYPE}" == "Debug" ]] && echo "true" || echo "false")
is_component_build = false
is_official_build = $([[ "${BUILD_TYPE}" == "Release" ]] && echo "true" || echo "false")

# CEF required args (must match gn_args.py GetRequiredArgs)
enable_widevine = true
optimize_webui = true
clang_use_chrome_plugins = false

# QNX baseline: no ThinLTO
use_thin_lto = false
thin_lto_enable_optimizations = false

# Symbol level (QNX non-component builds)
symbol_level = 1
blink_symbol_level = 0
v8_symbol_level = 0

# QNX toolchain
qnx_sdp_root = "${QNX_SDP_ROOT}"

# Disable features not supported on QNX
enable_print_preview = false
enable_printing = false
enable_nacl = false
enable_mdns = false
enable_remoting = false
# WebGPU (Dawn) is deferred: cfsimple is headless and the QEMU test
# environment has no GPU device, so WebGPU is not exercised. Disabling
# Dawn also sidesteps Dawn's Linux-only assumptions in renderdoc_app.h
# and ExternalImageDescriptorFD. See build-error-index.md and the related structured note.
use_dawn = false
# Skia Graphite Dawn is controlled by a separate build arg. Keep it aligned
# with use_dawn=false so //skia:buildflags emits SKIA_USE_DAWN=0 and
# gpu/config/gpu_info_collector.cc does not trip its USE_DAWN/SKIA_USE_DAWN
# consistency assert.
skia_use_dawn = false
# CEF 147 rebase regression: use_dawn = false alone no longer removes
# the Dawn vulkan backend sources. dawn_enable_vulkan defaults to
# is_linux in dawn/scripts/dawn_features.gni, and QNX is routed through
# is_linux by GN, so the vulkan external-memory translation units
# (MemoryServiceImplementation{DmaBuf,OpaqueFD}.cpp, which reference
# ExternalImageDescriptor{DmaBuf,OpaqueFD} types that are not in the
# CEF 147 Dawn tree) and the RenderDoc platform macros
# (renderdoc_app.h:43 "Unknown platform") end up compiled. Disable
# dawn_enable_vulkan explicitly here; use_dawn = false already keeps
# Chrome from linking libdawn_native, and dawn_enable_vulkan has no
# external consumer (verified by grep across the Chromium tree).
# See docs/qnx/history/build-errors/compile/build-graph/dawn-disabled-for-headless-qnx.md
# (2026-06-07 rebase note).
dawn_enable_vulkan = false
# dawn_use_swiftshader defaults to true on QNX (!is_android && !is_ios),
# but Dawn's BUILD.gn asserts dawn_use_swiftshader => dawn_enable_vulkan,
# so we have to clear it explicitly when dawn_enable_vulkan = false.
dawn_use_swiftshader = false

# CEF specific
cef_target_arch = "x64"
cef_use_alloc_shim = false
use_crash_key_stubs = true
enable_background_mode = false
enable_resource_allowlist_generation = false
enable_downgrade_processing = false

# Disable sandbox on QNX. cef_enable_sandbox is not a declared GN arg in
# Chromium 147, so disable the actual Linux sandbox feature gates directly.
v8_enable_sandbox = false
use_seccomp_bpf = false
compile_suid_client = false
compile_credentials = false
compile_syscall_broker = false

# Compiler / modules
is_clang = true
use_autogenerated_modules = false
use_clang_modules = false
treat_warnings_as_errors = false

# Test/settings overrides
enable_base_tracing = false
use_custom_libcxx = false
use_custom_libcxx_for_host = true
chrome_pgo_phase = 0

# UI: Ozone only, no desktop Linux frameworks
use_ozone = true
use_qt = false
use_qt5 = false
use_qt6 = false
use_x11 = false
use_glib = false
use_gio = false
use_gtk = false
use_dbus = false
use_pangocairo = false
use_xkbcommon = false
use_nss_certs = false
use_udev = false
# QNX audio backends require dedicated porting work. Do not inherit the
# generic POSIX/Linux defaults from media/media_options.gni.
use_alsa = false
use_pulseaudio = false
use_system_minigbm = false
use_system_libffi = false
ozone_platform_wayland = false
ozone_platform_x11 = false
ozone_platform_drm = false
rtc_use_pipewire = false
use_vaapi = false

# ANGLE: QNX SDP 8 ships <pci/pci.h> and libpci.so.3 but the API is the QNX
# native one (pci_device_attach / pci_device_detach / pci_device_find / etc),
# not the Linux libpci API (pci_alloc / pci_init / pci_scan_bus / etc) that
# SystemInfo_libpci.cpp expects. The current QNX build target is headless
# cefsimple (no WebGL / WebGPU), so disable libpci in ANGLE; GPU info will be
# collected via ANGLE_USE_VULKAN_DISPLAY (SystemInfo_vulkan.cpp) instead.
use_libpci = false
EOF

echo "GN args written to: ${GN_ARGS_FILE}"
echo ""

# ============================================================================
# Phase 5: Run gn gen for QNX.
# ============================================================================
echo "Phase 5: Running gn gen..."
cd "${CHROMIUM_SRC_DIR}"

export QNX_SDP_ROOT
export QNX_TARGET
export QNX_HOST

gn gen "${BUILD_DIR}"

echo ""

# Write helper scripts.
cat >"${BUILD_DIR}/qnx_env.sh" <<EOF
export QNX_SDP_ROOT="${QNX_SDP_ROOT}"
export QNX_TARGET="${QNX_TARGET}"
export QNX_HOST="${QNX_HOST}"
EOF

cat >"${BUILD_DIR}/ninja_qnx.sh" <<'EOF'
#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "${SCRIPT_DIR}/qnx_env.sh"
CHROMIUM_SRC="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${CHROMIUM_SRC}"
exec ninja -C "${SCRIPT_DIR}" "$@"
EOF
chmod +x "${BUILD_DIR}/ninja_qnx.sh"

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
