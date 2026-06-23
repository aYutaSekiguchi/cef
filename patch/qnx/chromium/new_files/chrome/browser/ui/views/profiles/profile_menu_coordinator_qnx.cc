// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/profile_menu_coordinator.h"

#include "base/check_deref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"

ProfileMenuCoordinator::ProfileMenuCoordinator(
    BrowserWindowInterface* browser, Profile* profile)
    : browser_(CHECK_DEREF(browser)), profile_(CHECK_DEREF(profile)) {}
ProfileMenuCoordinator::~ProfileMenuCoordinator() = default;
void ProfileMenuCoordinator::Show(bool is_source_accelerator,
                                  bool from_avatar_promo) {}
bool ProfileMenuCoordinator::IsShowing() const { return false; }
ProfileMenuViewBase* ProfileMenuCoordinator::GetProfileMenuViewBaseForTesting() {
  return nullptr;
}
BrowserWindowInterface* ProfileMenuCoordinator::GetBrowser() { return nullptr; }
Profile* ProfileMenuCoordinator::GetProfile() { return nullptr; }
