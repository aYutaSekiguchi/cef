// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#if BUILDFLAG(IS_QNX)
#include "chrome/browser/ui/views/frame/browser_native_widget_aura_qnx.h"

#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/views/widget/widget.h"

BrowserNativeWidgetAuraQnx::BrowserNativeWidgetAuraQnx(
    BrowserWidget* browser_widget,
    BrowserView* browser_view)
    : BrowserNativeWidgetAura(browser_widget, browser_view) {}

void BrowserNativeWidgetAuraQnx::InitNativeWidget(
    views::Widget::InitParams params) {
  // Run the standard BrowserNativeWidgetAura + DesktopNativeWidgetAura +
  // NativeWidgetAura chain which:
  //   - constructs the BrowserWidget's content_window_ (named
  //     "DesktopNativeWidgetAura - content window"), with delegate =
  //     DesktopNativeWidgetAura (a NativeWidgetAura / WindowDelegate),
  //   - creates the WindowTreeHost via DesktopWindowTreeHostPlatform, which
  //     internally constructs host_->window() (the *root* aura::Window,
  //     later renamed to "BrowserWidget" by DesktopNativeWidgetAura::
  //     OnNativeWidgetCreated). That root Window is created by
  //     WindowTreeHost::Create with `delegate = nullptr` -- there is no
  //     API to pass a delegate through the 2-arg WindowTreeHostPlatform
  //     constructor and no Window::SetDelegate existed before this fix.
  BrowserNativeWidgetAura::InitNativeWidget(std::move(params));

  // Phase 6 QNX input fix: the QNX marker [QNX_AURA_TARGET] reported
  // `target_name=BrowserWidget target_delegate_null=1` for the *root*
  // aura::Window (host_->window()) on cefsimple -- because that Window was
  // constructed with delegate=nullptr by WindowTreeHost::Create. As a
  // consequence, WED's hit-test on a mouse event found the root Window as
  // its target, and EventDispatcher::ProcessEvent skipped the EP_TARGET
  // dispatch (target->target_handler() was null), so event.handled stayed
  // 0 and the event never reached Widget/RWHVA/DOM. content_shell on the
  // same QNX platform does NOT hit this because its window tree only goes
  // up to the RWHVA aura::Window (which has a real delegate set in its
  // constructor), not the BrowserWidget root.
  //
  // We close the gap by calling the newly-added Window::SetDelegate on the
  // root aura::Window. This sets BOTH delegate_ (so the [QNX_AURA_TARGET]
  // delegate_null marker becomes 0) AND target_handler_ (so EP_TARGET
  // dispatches to NativeWidgetAura::OnEvent, which routes MouseEvent to
  // NativeWidgetAura::OnMouseEvent, which calls delegate_->OnMouseEvent,
  // i.e. Widget::OnMouseEvent, then down the BrowserView view tree to
  // RWHVA). Self = NativeWidgetAura via BrowserNativeWidgetAura, so the
  // existing OnMouseEvent chain is reused unchanged.
  if (host()) {
    aura::Window* root_window = host()->window();
    if (root_window && root_window->delegate() == nullptr) {
      root_window->SetDelegate(this);
    }
  }
}

#endif  // BUILDFLAG(IS_QNX)