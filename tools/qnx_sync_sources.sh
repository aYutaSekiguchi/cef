#!/usr/bin/env bash
# Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
# reserved. Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CEF_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
CHROMIUM_SRC_DIR="$(cd -- "${CEF_DIR}/.." && pwd)"
CHROMIUM_ROOT_DIR="$(cd -- "${CHROMIUM_SRC_DIR}/.." && pwd)"
PYTHON3="${PYTHON3:-python3}"

if [[ ! -f "${CHROMIUM_ROOT_DIR}/.gclient" ]]; then
  echo "ERROR: ${CHROMIUM_ROOT_DIR}/.gclient not found."
  echo "Run this script from a Chromium checkout managed by gclient."
  exit 1
fi

echo "QNX source sync helper"
echo "  Chromium src: ${CHROMIUM_SRC_DIR}"
echo "  Chromium root: ${CHROMIUM_ROOT_DIR}"
echo ""

echo "Step 1: Applying source-sync patches (.gitmodules / DEPS)..."
cd "${CHROMIUM_SRC_DIR}"
"${PYTHON3}" "${SCRIPT_DIR}/patcher.py" --patch-file qnx/chromium/qnx_source_sync

echo ""
echo "Step 2: Running gclient sync..."
cd "${CHROMIUM_ROOT_DIR}"
gclient sync "$@"

echo ""
echo "Success! QNX source dependencies are synced."
echo "Next step:"
echo "  cd ${CHROMIUM_SRC_DIR}"
echo "  ./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root <QNX_SDP_ROOT>"
