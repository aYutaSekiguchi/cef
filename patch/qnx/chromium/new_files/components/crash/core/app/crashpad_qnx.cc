// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/crash/core/app/crashpad.h"

#include <string>
#include <vector>

#include "base/debug/stack_trace.h"
#include "base/no_destructor.h"
#include "third_party/crashpad/crashpad/client/crashpad_client.h"

namespace crash_reporter {

bool InitializeCrashpad(bool initial_client, const std::string& process_type) {
  (void)initial_client;
  (void)process_type;
  return false;
}

crashpad::CrashpadClient& GetCrashpadClient() {
  static base::NoDestructor<crashpad::CrashpadClient> client;
  return *client;
}

void DestroyCrashpadClient() {}

#if !BUILDFLAG(IS_CHROMEOS)
void SetUploadConsent(bool consent) {
  (void)consent;
}
#endif

void GetReports(std::vector<Report>* reports) {
  reports->clear();
}

void RequestSingleCrashUpload(const std::string& local_id) {
  (void)local_id;
}

void DumpWithoutCrashing() {
  base::debug::StackTrace().Print();
}

std::optional<base::FilePath> GetCrashpadDatabasePath() {
  return std::nullopt;
}

void ClearReportsBetween(const base::Time& begin, const base::Time& end) {
  (void)begin;
  (void)end;
}

void GetReportsImpl(std::vector<Report>* reports) {
  reports->clear();
}

void RequestSingleCrashUploadImpl(const std::string& local_id) {
  (void)local_id;
}

void ClearReportsBetweenImpl(time_t begin, time_t end) {
  (void)begin;
  (void)end;
}

namespace internal {

crashpad::CrashReportDatabase* GetCrashReportDatabase() {
  return nullptr;
}

void SetCrashReportDatabaseForTesting(crashpad::CrashReportDatabase* database,
                                      base::FilePath* database_path) {
  (void)database;
  (void)database_path;
}

bool PlatformCrashpadInitialization(
    bool initial_client,
    bool browser_process,
    bool embedded_handler,
    const std::string& user_data_dir,
    const base::FilePath& exe_path,
    const std::vector<std::string>& initial_arguments,
    const std::vector<base::FilePath>& attachments,
    base::FilePath* database_path) {
  (void)initial_client;
  (void)browser_process;
  (void)embedded_handler;
  (void)user_data_dir;
  (void)exe_path;
  (void)initial_arguments;
  (void)attachments;
  (void)database_path;
  return false;
}

}  // namespace internal

}  // namespace crash_reporter
