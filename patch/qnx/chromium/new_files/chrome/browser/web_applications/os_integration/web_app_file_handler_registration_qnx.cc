// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/os_integration/web_app_file_handler_registration.h"

#include <utility>

#include "base/notimplemented.h"
#include "chrome/browser/web_applications/web_app_constants.h"

namespace web_app {

bool ShouldRegisterFileHandlersWithOs() {
  return false;
}

bool FileHandlingIconsSupportedByOs() {
  return false;
}

void RegisterFileHandlersWithOs(const webapps::AppId& app_id,
                                const std::string& app_name,
                                const base::FilePath& profile_path,
                                const apps::FileHandlers& file_handlers,
                                ResultCallback callback) {
  NOTIMPLEMENTED();
  std::move(callback).Run(Result::kError);
}

void UnregisterFileHandlersWithOs(const webapps::AppId& app_id,
                                  const base::FilePath& profile_path,
                                  ResultCallback callback) {
  NOTIMPLEMENTED();
  std::move(callback).Run(Result::kError);
}

}  // namespace web_app
