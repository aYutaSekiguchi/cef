// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/widget/desktop_aura/desktop_window_tree_host_platform.h"

namespace views {

aura::Window::Windows DesktopWindowTreeHostPlatform::GetAllOpenWindows() {
  return aura::Window::Windows();
}

aura::Window* DesktopWindowTreeHostPlatform::GetContentWindowForWidget(
    unsigned int widget_id) {
  return nullptr;
}

void DesktopWindowTreeHostPlatform::CleanUpWindowList(
    void (*cleanup)(aura::Window*)) {}

}  // namespace views
