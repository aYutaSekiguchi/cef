// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#if BUILDFLAG(IS_QNX)
#include "chrome/browser/ui/views/frame/browser_desktop_window_tree_host.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host_platform.h"

namespace {

class BrowserDesktopWindowTreeHostQnx
    : public views::DesktopWindowTreeHostPlatform,
      public BrowserDesktopWindowTreeHost {
 public:
  BrowserDesktopWindowTreeHostQnx(
      views::internal::NativeWidgetDelegate* native_widget_delegate,
      views::DesktopNativeWidgetAura* desktop_native_widget_aura)
      : views::DesktopWindowTreeHostPlatform(native_widget_delegate,
                                             desktop_native_widget_aura) {}

  BrowserDesktopWindowTreeHostQnx(const BrowserDesktopWindowTreeHostQnx&) =
      delete;
  BrowserDesktopWindowTreeHostQnx& operator=(
      const BrowserDesktopWindowTreeHostQnx&) = delete;

  views::DesktopWindowTreeHost* AsDesktopWindowTreeHost() override {
    return this;
  }

  bool UsesNativeSystemMenu() const override { return false; }
};

}  // namespace

// static
BrowserDesktopWindowTreeHost*
BrowserDesktopWindowTreeHost::CreateBrowserDesktopWindowTreeHost(
    views::internal::NativeWidgetDelegate* native_widget_delegate,
    views::DesktopNativeWidgetAura* desktop_native_widget_aura,
    BrowserView* browser_view,
    BrowserWidget* browser_widget) {
  return new BrowserDesktopWindowTreeHostQnx(native_widget_delegate,
                                             desktop_native_widget_aura);
}

#endif  // BUILDFLAG(IS_QNX)
