// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/font_render_params.h"

#include <string>

namespace gfx {

FontRenderParams GetFontRenderParams(const FontRenderParamsQuery& query,
                                     std::string* family_out) {
  return FontRenderParams();
}

void ClearFontRenderParamsCacheForTest() {}

float GetFontRenderParamsDeviceScaleFactor() {
  return 1.0f;
}

}  // namespace gfx
