// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_OZONE_PLATFORM_QNX_H_
#define UI_OZONE_PLATFORM_QNX_OZONE_PLATFORM_QNX_H_

#include "ui/ozone/public/ozone_platform.h"

namespace ui {

// Forward declaration — QnxSurfaceFactoryOzone lives in qnx_surface_factory.h.
// Defined in qnx_surface_factory.cc and compiled into the QNX platform target.
class QnxSurfaceFactoryOzone;

}  // namespace ui

// Phase 4: Browser/UI-side QNX Ozone Screen skeleton.
// - Owns QnxScreenContext (screen_context_t) and QnxWindowManager.
// - Provides CreatePlatformWindow() -> QnxWindow (screen_window_t).
// - Provides CreateScreen() -> QnxScreen.
// - EGL display/composition deferred to Phase 5.
// - GPU Mojo frame transport deferred to Phase 5/6.

namespace ui {

// Constructor hook for use in ui/ozone/platform/constructor_list.cc.
// Name is derived by generate_constructor_list.py from the platform name "qnx":
//   CreateOzonePlatform + "Qnx" = CreateOzonePlatformQnx()
OzonePlatform* CreateOzonePlatformQnx();

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_OZONE_PLATFORM_QNX_H_
