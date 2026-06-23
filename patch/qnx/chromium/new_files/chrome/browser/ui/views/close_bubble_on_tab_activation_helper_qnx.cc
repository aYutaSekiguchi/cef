// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/close_bubble_on_tab_activation_helper.h"

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

CloseBubbleOnTabActivationHelper::CloseBubbleOnTabActivationHelper(
    views::BubbleDialogDelegateView* owner_bubble,
    TabStripModel* tab_strip_model) {}
CloseBubbleOnTabActivationHelper::~CloseBubbleOnTabActivationHelper() = default;
void CloseBubbleOnTabActivationHelper::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {}
