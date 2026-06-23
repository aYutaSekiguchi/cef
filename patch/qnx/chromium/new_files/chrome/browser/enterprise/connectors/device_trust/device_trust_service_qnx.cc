// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/device_trust/device_trust_service.h"

namespace enterprise_connectors {

void DeviceTrustService::GetSignals(
    base::OnceCallback<void(base::DictValue)> callback) {
  std::move(callback).Run(base::DictValue());
}

}  // namespace enterprise_connectors
