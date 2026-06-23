// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/device_trust/device_trust_service_factory.h"

#include "chrome/browser/profiles/profile.h"

namespace enterprise_connectors {

DeviceTrustServiceFactory* DeviceTrustServiceFactory::GetInstance() {
  return nullptr;
}
DeviceTrustService* DeviceTrustServiceFactory::GetForProfile(Profile* profile) {
  return nullptr;
}

}  // namespace enterprise_connectors
