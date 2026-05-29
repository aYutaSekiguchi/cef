// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Polyfills for C++23 standard library features missing from QNX libc++.
// QNX SDP 8 ships with LLVM 18.1 libc++ which does not include all C++23
// additions. This file provides minimal polyfills gated by feature-test
// macros so they automatically become no-ops when QNX updates its libc++.
//
// This file is force-included via -include in the QNX toolchain CXXFLAGS
// (build/toolchain/qnx/BUILD.gn).  Only C++ compilation is affected.

#ifndef BUILD_CONFIG_QNX_QNX_STD_POLYFILL_H_
#define BUILD_CONFIG_QNX_QNX_STD_POLYFILL_H_

// This header is C++-only.  C compilation should skip entirely.
#ifdef __cplusplus

// QNX sys/types.h defines minor() and major() macros under __EXT_UNIX_MISC
// that collide with method names in protobuf-generated and perfetto-generated
// C++ code. The macros are intended for C only, but some QNX SDK versions
// lack the !defined(__cplusplus) guard. Undef them here to avoid collisions.
// This is force-included early in the compile chain, before sys/types.h is
// typically pulled in via perfetto or other headers.
#ifdef minor
#undef minor
#endif
#ifdef major
#undef major
#endif

#include <version>

// std::ranges::contains was added in C++23 (P2302R1, feature-test macro
// __cpp_lib_ranges_contains = 202207L).  QNX libc++ 18.1 does not ship
// this function.  When QNX eventually ships a libc++ that defines
// __cpp_lib_ranges_contains >= 202207L this entire block is skipped,
// avoiding ODR conflicts.
#if !defined(__cpp_lib_ranges_contains) || \
    __cpp_lib_ranges_contains < 202207L

#include <algorithm>
#include <utility>

// NOTE: We avoid depending on std::ranges concepts (indirect_binary_predicate,
// projected, etc.) because QNX libc++ 18.1 also lacks those.  The polyfill
// uses a simple template without the standard constraints; QNX builds of
// Chromium compile with -Wno-unused-parameter so mismatches manifest as
// compile errors in the caller, not silent miscompilation.

namespace std::ranges {

// Range overload of contains().  Returns true iff |value| is equal to an
// element in |r|.  A projection |proj| is applied to each element before
// comparison.
template <class R, class T, class Proj = identity>
[[nodiscard]] constexpr bool contains(R&& r, const T& value, Proj proj = {}) {
  return ranges::find(std::forward<R>(r), value, std::move(proj)) !=
         ranges::end(r);
}

}  // namespace std::ranges

#endif  // __cpp_lib_ranges_contains

#endif  // __cplusplus
#endif  // BUILD_CONFIG_QNX_QNX_STD_POLYFILL_H_