// Copyright 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "chrome/browser/platform_util.h"

#include "base/files/file_path.h"
#include "chrome/browser/platform_util_internal.h"
#include "url/gurl.h"

class Profile;

namespace platform_util {

namespace internal {

void PlatformOpenVerifiedItem(const base::FilePath& path, OpenItemType type) {
  // QNX does not yet provide a desktop shell/file-manager integration for this
  // port. Treat open requests as handled so callers do not retain unresolved
  // platform symbols; higher-level callers already validate the path/type and
  // complete their callbacks in platform_util.cc.
}

}  // namespace internal

void ShowItemInFolder(Profile* profile, const base::FilePath& full_path) {
  // No-op until a QNX shell/file-manager integration exists.
}

void OpenExternal(const GURL& url) {
  // No-op until QNX URL handler integration exists.
}

}  // namespace platform_util
