// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASHPAD_HANDLER_QNX_CRASH_REPORT_EXCEPTION_HANDLER_H_
#define CRASHPAD_HANDLER_QNX_CRASH_REPORT_EXCEPTION_HANDLER_H_

#include <map>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "client/crash_report_database.h"
#include "handler/crash_report_upload_thread.h"
#include "handler/qnx/exception_handler_server.h"
#include "handler/user_stream_data_source.h"

namespace crashpad {

class CrashReportExceptionHandler final : public ExceptionHandlerServer::Delegate {
 public:
  CrashReportExceptionHandler(
      CrashReportDatabase* database,
      CrashReportUploadThread* upload_thread,
      const std::map<std::string, std::string>* process_annotations,
      const UserStreamDataSources* user_stream_data_sources) {
    (void)database;
    (void)upload_thread;
    (void)process_annotations;
    (void)user_stream_data_sources;
  }

  CrashReportExceptionHandler(
      CrashReportDatabase* database,
      CrashReportUploadThread* upload_thread,
      const std::map<std::string, std::string>* process_annotations,
      const std::vector<base::FilePath>* attachments,
      bool write_minidump_to_database,
      bool write_minidump_to_log,
      const UserStreamDataSources* user_stream_data_sources) {
    (void)database;
    (void)upload_thread;
    (void)process_annotations;
    (void)attachments;
    (void)write_minidump_to_database;
    (void)write_minidump_to_log;
    (void)user_stream_data_sources;
  }

  CrashReportExceptionHandler(const CrashReportExceptionHandler&) = delete;
  CrashReportExceptionHandler& operator=(const CrashReportExceptionHandler&) = delete;
  ~CrashReportExceptionHandler() override = default;

  bool HandleException(pid_t client_process_id,
                       uid_t client_uid,
                       const ExceptionHandlerProtocol::ClientInformation& info,
                       VMAddress requesting_thread_stack_address = 0,
                       pid_t* requesting_thread_id = nullptr,
                       UUID* local_report_id = nullptr) override {
    (void)client_process_id;
    (void)client_uid;
    (void)info;
    (void)requesting_thread_stack_address;
    if (requesting_thread_id) {
      *requesting_thread_id = -1;
    }
    (void)local_report_id;
    return false;
  }

  bool HandleExceptionWithBroker(
      pid_t client_process_id,
      uid_t client_uid,
      const ExceptionHandlerProtocol::ClientInformation& info,
      int broker_sock,
      UUID* local_report_id = nullptr) override {
    (void)client_process_id;
    (void)client_uid;
    (void)info;
    (void)broker_sock;
    (void)local_report_id;
    return false;
  }
};

}  // namespace crashpad

#endif  // CRASHPAD_HANDLER_QNX_CRASH_REPORT_EXCEPTION_HANDLER_H_
