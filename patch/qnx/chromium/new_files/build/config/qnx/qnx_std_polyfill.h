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
// C++ code. The macros are intended for C only, but QNX SDK versions lack the
// !defined(__cplusplus) guard. Include sys/types.h here and undef them
// immediately after to prevent collision in all downstream code.
#include <sys/types.h>
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
#include <functional>
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
  for (auto&& element : r) {
    if (std::invoke(proj, element) == value) {
      return true;
    }
  }
  return false;
}

}  // namespace std::ranges

#endif  // __cpp_lib_ranges_contains

// std::atomic_ref was introduced in C++20 (P0019R8, feature-test macro
// __cpp_lib_atomic_ref = 201806L).  QNX SDP 8 libc++ does not include it.
// This polyfill delegates to std::atomic<T>* via reinterpret_cast, which is
// safe because std::atomic<T> has the same object representation as T for
// trivially copyable types.
#if !defined(__cpp_lib_atomic_ref) || \
    __cpp_lib_atomic_ref < 201806L

namespace std {

template <typename T>
class atomic_ref {
  static_assert(std::is_trivially_copyable_v<T>,
                "atomic_ref requires trivially copyable type");

  T* ptr_;

 public:
  explicit atomic_ref(T& obj) : ptr_(&obj) {}

  void store(T value, memory_order order = memory_order_seq_cst) const {
    reinterpret_cast<atomic<T>*>(ptr_)->store(value, order);
  }

  T load(memory_order order = memory_order_seq_cst) const {
    return reinterpret_cast<const atomic<T>*>(
        const_cast<const T*>(ptr_))->load(order);
  }

  T exchange(T value, memory_order order = memory_order_seq_cst) const {
    return reinterpret_cast<atomic<T>*>(ptr_)->exchange(value, order);
  }

  bool compare_exchange_strong(T& expected, T desired,
                               memory_order success,
                               memory_order failure) const {
    return reinterpret_cast<atomic<T>*>(ptr_)->compare_exchange_strong(
        expected, desired, success, failure);
  }

  bool compare_exchange_strong(T& expected, T desired,
                               memory_order order =
                                   memory_order_seq_cst) const {
    return reinterpret_cast<atomic<T>*>(ptr_)->compare_exchange_strong(
        expected, desired, order);
  }

  T fetch_add(T value, memory_order order = memory_order_seq_cst) const {
    return reinterpret_cast<atomic<T>*>(ptr_)->fetch_add(value, order);
  }

  T fetch_or(T value, memory_order order = memory_order_seq_cst) const {
    return reinterpret_cast<atomic<T>*>(ptr_)->fetch_or(value, order);
  }

  static constexpr size_t required_alignment = alignof(T);
};

template <typename T>
atomic_ref(T&) -> atomic_ref<T>;

}  // namespace std

#endif  // __cpp_lib_atomic_ref

#endif  // __cplusplus
#endif  // BUILD_CONFIG_QNX_QNX_STD_POLYFILL_H_
