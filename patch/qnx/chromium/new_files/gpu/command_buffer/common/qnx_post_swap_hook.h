// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_COMMON_QNX_POST_SWAP_HOOK_H_
#define GPU_COMMAND_BUFFER_COMMON_QNX_POST_SWAP_HOOK_H_

#include "base/functional/callback.h"
#include "gpu/command_buffer/common/gpu_command_buffer_common_export.h"
#include "ui/gfx/geometry/size.h"

namespace viz {

// QNX swap hook signature. The pre-swap callback runs on the GPU thread
// while the compositor framebuffer is current; the post-swap callback is a
// completion marker after presentation has been scheduled.
//
// The hooks are global (per-process) registrations. There is typically only
// one SkiaOutputSurfaceImpl per process, so global storage is sufficient.
using QnxSwapHook = base::RepeatingCallback<void(const gfx::Size&)>;

// Installs the hook. Calling this replaces any previously installed hook.
// Declared here so that ui/ozone can register the hook without introducing
// a ui/ozone -> components/viz/service dependency cycle.
GPU_COMMAND_BUFFER_COMMON_EXPORT void SetQnxPostSwapHook(QnxSwapHook hook);

// The pre-swap hook runs on the GPU thread while the compositor framebuffer
// is still current. It is the safe capture point for QNX DMAbuf export.
GPU_COMMAND_BUFFER_COMMON_EXPORT void SetQnxPreSwapHook(QnxSwapHook hook);

namespace internal {

// Invoked by SkiaOutputSurfaceImpl::DidSwapBuffersComplete on the viz
// thread after the renderer-side swap acknowledgement is dispatched. Returns
// true if a hook was registered and called.
GPU_COMMAND_BUFFER_COMMON_EXPORT bool RunQnxPostSwapHook(const gfx::Size& pixel_size);

GPU_COMMAND_BUFFER_COMMON_EXPORT bool RunQnxPreSwapHook(const gfx::Size& pixel_size);

}  // namespace internal

}  // namespace viz

#endif  // GPU_COMMAND_BUFFER_COMMON_QNX_POST_SWAP_HOOK_H_
