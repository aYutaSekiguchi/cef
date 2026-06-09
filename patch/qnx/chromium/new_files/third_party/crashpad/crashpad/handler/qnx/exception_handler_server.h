// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASHPAD_HANDLER_QNX_EXCEPTION_HANDLER_SERVER_H_
#define CRASHPAD_HANDLER_QNX_EXCEPTION_HANDLER_SERVER_H_

#include <sys/types.h>

#include "util/file/file_io.h"
#include "util/linux/exception_handler_protocol.h"
#include "util/misc/address_types.h"
#include "util/misc/uuid.h"

namespace crashpad {

class ExceptionHandlerServer {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    virtual bool HandleException(
        pid_t client_process_id,
        uid_t client_uid,
        const ExceptionHandlerProtocol::ClientInformation& info,
        VMAddress requesting_thread_stack_address = 0,
        pid_t* requesting_thread_id = nullptr,
        UUID* local_report_id = nullptr) = 0;

    virtual bool HandleExceptionWithBroker(
        pid_t client_process_id,
        uid_t client_uid,
        const ExceptionHandlerProtocol::ClientInformation& info,
        int broker_sock,
        UUID* local_report_id = nullptr) = 0;
  };

  ExceptionHandlerServer() = default;
  ExceptionHandlerServer(const ExceptionHandlerServer&) = delete;
  ExceptionHandlerServer& operator=(const ExceptionHandlerServer&) = delete;
  ~ExceptionHandlerServer() = default;

  bool InitializeWithClient(ScopedFileHandle sock, bool multiple_clients) {
    (void)sock;
    (void)multiple_clients;
    return true;
  }

  void Run(Delegate* delegate) { (void)delegate; }
  void Stop() {}
};

}  // namespace crashpad

#endif  // CRASHPAD_HANDLER_QNX_EXCEPTION_HANDLER_SERVER_H_
