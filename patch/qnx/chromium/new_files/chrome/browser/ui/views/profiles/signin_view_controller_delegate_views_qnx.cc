// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/signin/signin_view_controller_delegate.h"

#include "chrome/browser/ui/signin/signin_view_controller.h"
#include "chrome/browser/ui/webui/signin/history_sync_optin_helper.h"
#include "chrome/browser/ui/webui/signin/signin_utils.h"

// Stub implementations for QNX. Sign-in UI is not available on QNX.
SigninViewControllerDelegate*
SigninViewControllerDelegate::CreateSyncConfirmationDelegate(
    Browser* browser,
    SyncConfirmationStyle style,
    bool is_sync_promo) {
  return nullptr;
}

SigninViewControllerDelegate*
SigninViewControllerDelegate::CreateSyncHistoryOptInDelegate(
    Browser* browser,
    bool should_close_modal_dialog,
    HistorySyncOptinLaunchContext launch_context,
    HistorySyncOptinHelper::FlowCompletedCallback callback) {
  return nullptr;
}

SigninViewControllerDelegate*
SigninViewControllerDelegate::CreateSigninErrorDelegate(Browser* browser) {
  return nullptr;
}

SigninViewControllerDelegate*
SigninViewControllerDelegate::CreateProfileCustomizationDelegate(
    Browser* browser,
    bool is_local_profile_creation,
    bool show_profile_switch_iph,
    bool show_supervised_user_iph) {
  return nullptr;
}

SigninViewControllerDelegate*
SigninViewControllerDelegate::CreateSignoutConfirmationDelegate(
    Browser* browser,
    ChromeSignoutConfirmationPromptVariant variant,
    size_t unsynced_data_count,
    SignoutConfirmationCallback callback) {
  return nullptr;
}
