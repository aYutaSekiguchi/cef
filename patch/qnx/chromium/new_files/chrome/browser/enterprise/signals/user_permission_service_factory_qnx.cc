// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/signals/user_permission_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "components/device_signals/core/browser/user_permission_service.h"

namespace enterprise_signals {

UserPermissionServiceFactory* UserPermissionServiceFactory::GetInstance() {
  return nullptr;
}
device_signals::UserPermissionService* UserPermissionServiceFactory::GetForProfile(
    Profile* profile) {
  return nullptr;
}

}  // namespace enterprise_signals
