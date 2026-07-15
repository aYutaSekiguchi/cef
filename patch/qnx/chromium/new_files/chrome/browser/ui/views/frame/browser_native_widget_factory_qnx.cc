// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#if BUILDFLAG(IS_QNX)
#include "chrome/browser/ui/views/frame/browser_native_widget_factory.h"
#include "chrome/browser/ui/views/frame/browser_native_widget_aura_qnx.h"

BrowserNativeWidget* BrowserNativeWidgetFactory::Create(
    BrowserWidget* browser_widget,
    BrowserView* browser_view) {
  // Phase 6 input fix: use the QNX-specific BrowserNativeWidgetAuraQnx
  // subclass which guarantees the BrowserWidget's aura::Window has a
  // non-null WindowDelegate (see browser_native_widget_aura_qnx.cc for the
  // marker-observed rationale).
  return new BrowserNativeWidgetAuraQnx(browser_widget, browser_view);
}
#endif  // BUILDFLAG(IS_QNX)
