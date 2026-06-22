// Copyright 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "printing/units.h"

#include "base/numerics/safe_conversions.h"

namespace printing {

int ConvertUnit(float value, int old_unit, int new_unit) {
  return base::ClampRound(ConvertUnitFloat(value, old_unit, new_unit));
}

float ConvertUnitFloat(float value, float old_unit, float new_unit) {
  return value * new_unit / old_unit;
}

}  // namespace printing
