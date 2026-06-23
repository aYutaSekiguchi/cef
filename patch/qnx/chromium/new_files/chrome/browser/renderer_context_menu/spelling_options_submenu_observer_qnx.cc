// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_context_menu/spelling_options_submenu_observer.h"

#include <cstddef>

#include "components/renderer_context_menu/render_view_context_menu_proxy.h"
#include "ui/menus/simple_menu_model.h"

SpellingOptionsSubMenuObserver::SpellingOptionsSubMenuObserver(
    RenderViewContextMenuProxy* proxy,
    ui::SimpleMenuModel::Delegate* delegate,
    int group_id)
    : proxy_(proxy), submenu_model_(delegate) {}
SpellingOptionsSubMenuObserver::~SpellingOptionsSubMenuObserver() = default;
void SpellingOptionsSubMenuObserver::InitMenu(
    const content::ContextMenuParams& params) {}
bool SpellingOptionsSubMenuObserver::IsCommandIdSupported(int command_id) {
  return false;
}
bool SpellingOptionsSubMenuObserver::IsCommandIdChecked(int command_id) {
  return false;
}
bool SpellingOptionsSubMenuObserver::IsCommandIdEnabled(int command_id) {
  return false;
}
void SpellingOptionsSubMenuObserver::ExecuteCommand(int command_id) {}
