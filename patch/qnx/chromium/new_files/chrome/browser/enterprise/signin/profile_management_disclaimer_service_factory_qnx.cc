// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/signin/profile_management_disclaimer_service_factory.h"

#include "chrome/browser/profiles/profile.h"

ProfileManagementDisclaimerServiceFactory*
ProfileManagementDisclaimerServiceFactory::GetInstance() {
  return nullptr;
}
ProfileManagementDisclaimerService*
ProfileManagementDisclaimerServiceFactory::GetForProfile(Profile* profile) {
  return nullptr;
}
bool ProfileManagementDisclaimerServiceFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;
}
