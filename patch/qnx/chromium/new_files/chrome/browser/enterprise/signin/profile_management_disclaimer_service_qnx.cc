// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/signin/profile_management_disclaimer_service.h"

#include "chrome/browser/profiles/profile.h"
#include "components/signin/public/base/signin_prefs.h"

ProfileManagementDisclaimerService::ProfileManagementDisclaimerService(
    Profile* profile)
    : profile_(*profile),
      state_(std::make_unique<ResetableState>()),
      signin_prefs_(*profile->GetPrefs()) {}
ProfileManagementDisclaimerService::~ProfileManagementDisclaimerService() =
    default;
void ProfileManagementDisclaimerService::EnsureManagedProfileForAccount(
    const CoreAccountId& account_id,
    signin_metrics::AccessPoint access_point,
    base::OnceCallback<void(Profile*, bool)> callback) {}
const CoreAccountId&
ProfileManagementDisclaimerService::GetAccountBeingConsideredForManagementIfAny()
    const {
  static const CoreAccountId empty;
  return empty;
}
bool ProfileManagementDisclaimerService::StopCurrentProcessIfPossible() {
  return false;
}
base::ScopedClosureRunner
ProfileManagementDisclaimerService::AutoAcceptManagementDisclaimerUntilReset() {
  return base::ScopedClosureRunner();
}
base::ScopedClosureRunner
ProfileManagementDisclaimerService::DisableManagementDisclaimerUntilReset() {
  return base::ScopedClosureRunner();
}
