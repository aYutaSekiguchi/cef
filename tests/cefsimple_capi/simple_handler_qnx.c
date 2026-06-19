// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/cefsimple_capi/simple_handler.h"

void simple_handler_platform_title_change(simple_handler_t* handler,
                                          cef_browser_t* browser,
                                          const cef_string_t* title) {
  // QNX does not use the X11 title update path. The CEF sample can run without
  // a platform-specific title update hook.
}
