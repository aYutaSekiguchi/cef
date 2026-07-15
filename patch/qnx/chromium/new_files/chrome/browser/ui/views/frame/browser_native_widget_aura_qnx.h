// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_NATIVE_WIDGET_AURA_QNX_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_NATIVE_WIDGET_AURA_QNX_H_

#include "chrome/browser/ui/views/frame/browser_native_widget_aura.h"

// Provides the window frame for the Chrome/CEF browser window on QNX.
//
// Phase 6 input fix: the BrowserWidget aura::Window's WindowDelegate was
// observed to be null on QNX (the [QNX_AURA_TARGET] Window::CanAcceptEvent
// marker reported `delegate_null=1`), causing mouse input dispatched by
// EventProcessor to be dropped silently. This QNX-specific subclass installs
// itself as the aura::Window's WindowDelegate after super::InitNativeWidget
// runs, so the dispatched event reaches a handler (which forwards via
// OnEvent -> BrowserView/Aura tree -> RenderWidgetHostViewAura -> DOM).
class BrowserNativeWidgetAuraQnx : public BrowserNativeWidgetAura {
 public:
  BrowserNativeWidgetAuraQnx(BrowserWidget* browser_widget,
                             BrowserView* browser_view);

  BrowserNativeWidgetAuraQnx(const BrowserNativeWidgetAuraQnx&) = delete;
  BrowserNativeWidgetAuraQnx& operator=(const BrowserNativeWidgetAuraQnx&) =
      delete;

  // BrowserNativeWidgetAura:
  void InitNativeWidget(views::Widget::InitParams params) override;

 protected:
  ~BrowserNativeWidgetAuraQnx() override = default;
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_BROWSER_NATIVE_WIDGET_AURA_QNX_H_
