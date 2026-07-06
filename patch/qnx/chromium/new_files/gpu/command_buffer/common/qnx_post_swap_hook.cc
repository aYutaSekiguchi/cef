// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/common/qnx_post_swap_hook.h"

#include "base/no_destructor.h"

namespace viz {

namespace {

QnxPostSwapHook& GetQnxPostSwapHookStorage() {
  static base::NoDestructor<QnxPostSwapHook> hook;
  return *hook;
}

}  // namespace

void SetQnxPostSwapHook(QnxPostSwapHook hook) {
  GetQnxPostSwapHookStorage() = std::move(hook);
}

namespace internal {

bool RunQnxPostSwapHook(const gfx::Size& pixel_size) {
  if (GetQnxPostSwapHookStorage()) {
    GetQnxPostSwapHookStorage().Run(pixel_size);
    return true;
  }
  return false;
}

}  // namespace internal

}  // namespace viz