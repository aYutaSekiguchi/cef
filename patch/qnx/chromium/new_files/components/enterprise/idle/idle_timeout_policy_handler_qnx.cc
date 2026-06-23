// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/idle/idle_timeout_policy_handler.h"

namespace enterprise_idle {

IdleTimeoutPolicyHandler::IdleTimeoutPolicyHandler() = default;
IdleTimeoutPolicyHandler::~IdleTimeoutPolicyHandler() = default;

IdleTimeoutActionsPolicyHandler::IdleTimeoutActionsPolicyHandler(
    policy::Schema schema) {}
IdleTimeoutActionsPolicyHandler::~IdleTimeoutActionsPolicyHandler() = default;

}  // namespace enterprise_idle
