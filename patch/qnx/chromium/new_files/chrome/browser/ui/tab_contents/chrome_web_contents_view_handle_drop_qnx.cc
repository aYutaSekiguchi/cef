// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/tab_contents/chrome_web_contents_view_handle_drop.h"

#include "base/functional/callback.h"

void HandleOnPerformingDrop(
    content::WebContents* web_contents,
    const content::DropData& drop_data,
    base::OnceCallback<void(std::optional<content::DropData>)> callback) {}
