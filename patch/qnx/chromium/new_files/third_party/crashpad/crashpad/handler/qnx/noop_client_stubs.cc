// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/crashpad/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/crashpad/client/crashpad_client.h"
#include "third_party/crashpad/crashpad/util/misc/paths.h"
#include "third_party/crashpad/crashpad/util/net/http_transport.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace crashpad {

CrashpadClient::CrashpadClient() = default;
CrashpadClient::~CrashpadClient() = default;

bool CrashpadClient::StartHandler(
    const base::FilePath& handler,
    const base::FilePath& database,
    const base::FilePath& metrics_dir,
    const std::string& url,
    const std::map<std::string, std::string>& annotations,
    const std::vector<std::string>& arguments,
    bool restartable,
    bool asynchronous_start,
    const std::vector<base::FilePath>& attachments) {
  return false;
}

// static
std::unique_ptr<CrashReportDatabase> CrashReportDatabase::Initialize(
    const base::FilePath& path) {
  return nullptr;
}

// static
std::unique_ptr<CrashReportDatabase> CrashReportDatabase::InitializeWithoutCreating(
    const base::FilePath& path) {
  return nullptr;
}

// static
std::unique_ptr<HTTPTransport> HTTPTransport::Create() {
  return nullptr;
}

// static
bool Paths::Executable(base::FilePath* path) {
  return false;
}

}  // namespace crashpad
