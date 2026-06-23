// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/startup/first_run_service_factory.h"

#include "content/public/browser/browser_context.h"

FirstRunServiceFactory* FirstRunServiceFactory::GetInstance() { return nullptr; }
FirstRunService* FirstRunServiceFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return nullptr;
}
