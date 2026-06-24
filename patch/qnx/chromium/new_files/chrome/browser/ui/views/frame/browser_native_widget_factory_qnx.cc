// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#if BUILDFLAG(IS_QNX)
#include "chrome/browser/ui/views/frame/browser_native_widget_factory.h"

#include <memory>

BrowserNativeWidget* BrowserNativeWidgetFactory::CreateBrowserNativeWidget(
    BrowserWidget* browser_widget,
    BrowserView* browser_view) {
  return nullptr;
}

BrowserNativeWidget* BrowserNativeWidgetFactory::Create(
    BrowserWidget* browser_widget,
    BrowserView* browser_view) {
  return nullptr;
}
#endif  // BUILDFLAG(IS_QNX)
