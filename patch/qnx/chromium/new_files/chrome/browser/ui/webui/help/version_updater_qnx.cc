// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/help/version_updater.h"

#include "content/public/browser/web_contents.h"

std::unique_ptr<VersionUpdater> VersionUpdater::Create(
    content::WebContents* web_contents) {
  return nullptr;
}
