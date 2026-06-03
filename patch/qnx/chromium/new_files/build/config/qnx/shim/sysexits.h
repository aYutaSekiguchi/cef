// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// QNX sysroot shim for <sysexits.h>.
//
// Problem: QNX SDP 8 does not ship <sysexits.h>. LLVM 10's
// CrashRecoveryContext.cpp and other third_party code that follows
// the BSD sysexits convention use EX_IOERR from this header.
//
// Fix: Provide a minimal sysexits.h with just the EX_* constants
// that have been observed in Chromium / SwiftShader / LLVM code. The
// values are the standard BSD sysexits.h values defined in
// <sysexits.h> on Linux/macOS.
//
// This file is found via the same -I path as the other shim headers
// (see build/toolchain/qnx/BUILD.gn: -I${_qnx_shim_dir}). Place new
// EX_* #defines here as the build surfaces new needs.

#ifndef BUILD_CONFIG_QNX_SHIM_SYSEXITS_H_
#define BUILD_CONFIG_QNX_SHIM_SYSEXITS_H_

#define EX_OK           0  // successful termination
#define EX_USAGE       64  // command line usage error
#define EX_DATAERR     65  // data format error
#define EX_NOINPUT     66  // cannot open input
#define EX_NOUSER      67  // addressee unknown
#define EX_NOHOST      68  // host name unknown
#define EX_UNAVAILABLE 69  // service unavailable
#define EX_SOFTWARE    70  // internal software error
#define EX_OSERR       71  // system error (e.g., can't fork)
#define EX_OSFILE      72  // critical OS file missing
#define EX_CANTCREAT   73  // can't create user output file
#define EX_IOERR       74  // input/output error
#define EX_TEMPFAIL    75  // temp failure; user is invited to retry
#define EX_PROTOCOL    76  // remote error in protocol
#define EX_NOPERM      77  // permission denied
#define EX_CONFIG      78  // configuration error

#endif  // BUILD_CONFIG_QNX_SHIM_SYSEXITS_H_
