// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_COMMON_QNX_POST_SWAP_HOOK_H_
#define GPU_COMMAND_BUFFER_COMMON_QNX_POST_SWAP_HOOK_H_

#include "base/functional/callback.h"
#include "gpu/command_buffer/common/gpu_command_buffer_common_export.h"
#include "ui/gfx/geometry/size.h"

namespace viz {

// QNX post-swap hook signature.
//
// Set by ui/ozone/platform/qnx in the GPU process to observe the actual
// SkiaRenderer swap completion path. Fires on the viz thread after the
// underlying GL/EGL swap has completed on the GPU thread; the
// renderer-side swap acknowledgement to the browser process is dispatched
// normally afterwards.
//
// Phase 6a: the hook is used to validate that real-content export is
// reachable from the swap completion path. Phase 6b will use the hook to
// capture the actual rendered framebuffer content into a DMAbuf that the
// QNX Ozone GPU-side producer can submit to the browser host.
//
// The hook is a global (per-process) registration. There is typically only
// one SkiaOutputSurfaceImpl per process, so a global registration is
// sufficient. Pass base::RepeatingCallback<void(const gfx::Size&)>() to
// clear the hook.
using QnxPostSwapHook = base::RepeatingCallback<void(const gfx::Size&)>;

// Installs the hook. Calling this replaces any previously installed hook.
// Declared here so that ui/ozone can register the hook without introducing
// a ui/ozone -> components/viz/service dependency cycle.
GPU_COMMAND_BUFFER_COMMON_EXPORT void SetQnxPostSwapHook(QnxPostSwapHook hook);

namespace internal {

// Invoked by SkiaOutputSurfaceImpl::DidSwapBuffersComplete on the viz
// thread after the renderer-side swap acknowledgement is dispatched. Returns
// true if a hook was registered and called.
GPU_COMMAND_BUFFER_COMMON_EXPORT bool RunQnxPostSwapHook(const gfx::Size& pixel_size);

}  // namespace internal

}  // namespace viz

#endif  // GPU_COMMAND_BUFFER_COMMON_QNX_POST_SWAP_HOOK_H_