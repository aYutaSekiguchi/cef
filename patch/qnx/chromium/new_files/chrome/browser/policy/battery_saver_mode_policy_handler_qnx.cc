// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/battery_saver_policy_handler.h"

#include "components/policy/core/common/policy_map.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_value_map.h"

namespace policy {

BatterySaverPolicyHandler::BatterySaverPolicyHandler()
    : TypeCheckingPolicyHandler(key::kBatterySaverModeAvailability,
                                base::Value::Type::INTEGER) {}

void BatterySaverPolicyHandler::ApplyPolicySettings(
    const PolicyMap& policies,
    PrefValueMap* prefs) {}

}  // namespace policy
