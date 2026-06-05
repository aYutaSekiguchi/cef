// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license
// found in the LICENSE file.

#ifndef BUILD_CONFIG_QNX_QNX_FFMPEG_COMPAT_H_
#define BUILD_CONFIG_QNX_QNX_FFMPEG_COMPAT_H_

// FFmpeg on QNX: third_party/ffmpeg/BUILD.gn's ffmpeg_internal target
// declares in its `defines` array:
//
//     _POSIX_C_SOURCE=200112
//     _XOPEN_SOURCE=600
//
// because the bundled FFmpeg source tree targets XPG/SUSv3. The QNX
// toolchain (build/toolchain/qnx/BUILD.gn) separately forces
// -D_POSIX_C_SOURCE=200809L globally so that QNX sysroot headers
// expose strdup / dev_t / uid_t / gid_t / etc. (Phase 2-1 fixes in
// cef/docs/qnx/history/archive/plan.md, items #5 and "sys/stat.h
// types undefined"). Lowering the toolchain's value would re-break
// that surface for every other QNX TU.
//
// In the gcc_toolchain command template (gcc_toolchain.gni:326), the
// toolchain's -D_POSIX_C_SOURCE=200809L is applied last, so it wins
// the command-line macro state. QNX's
// /usr/include/sys/platform.h:163-165 then rejects the resulting
// combination _XOPEN_SOURCE=600 + _POSIX_C_SOURCE=200809L with:
//
//     #elif _XOPEN_SOURCE-0 == 600
//         #if _POSIX_C_SOURCE-0 > 200112
//         #error This POSIX_C_SOURCE is unsuported with XOPEN_SOURCE
//         #endif
//
// This shim is force-included into the ffmpeg_internal target only
// (see third_party/ffmpeg/BUILD.gn's `if (is_qnx) cflags += ...`).
// clang's -D flags are all applied first, then -include files are
// processed, so the `#define` below overrides the toolchain's
// command-line -D_POSIX_C_SOURCE=200809L for FFmpeg TUs only.
// Every other QNX TU keeps the toolchain-wide 200809L.

#if defined(FFMPEG_CONFIGURATION)
    #undef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 200112
#endif

#endif  // BUILD_CONFIG_QNX_QNX_FFMPEG_COMPAT_H_
