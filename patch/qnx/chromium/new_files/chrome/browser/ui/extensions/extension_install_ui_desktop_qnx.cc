// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/extensions/extension_install_ui_desktop.h"

#include "content/public/browser/browser_context.h"

ExtensionInstallUIDesktop::ExtensionInstallUIDesktop(
    content::BrowserContext* context) {}
ExtensionInstallUIDesktop::~ExtensionInstallUIDesktop() = default;
void ExtensionInstallUIDesktop::OnInstallSuccess(
    scoped_refptr<const extensions::Extension> extension,
    const SkBitmap* icon) {}
void ExtensionInstallUIDesktop::OnInstallFailure(
    const extensions::CrxInstallError& error) {}
