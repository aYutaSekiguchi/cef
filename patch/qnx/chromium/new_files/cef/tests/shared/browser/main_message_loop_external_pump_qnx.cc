// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/shared/browser/main_message_loop_external_pump.h"

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"

namespace client {

namespace {

class MainMessageLoopExternalPumpQnx : public MainMessageLoopExternalPump {
 public:
  MainMessageLoopExternalPumpQnx() = default;

  void OnScheduleMessagePumpWork(int64_t delay_ms) override {
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(
                           &MainMessageLoopExternalPumpQnx::OnScheduleWork,
                           base::Unretained(this), delay_ms)),
                       0);
  }

 protected:
  void SetTimer(int64_t delay_ms) override {
    timer_pending_ = true;
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(
                           &MainMessageLoopExternalPumpQnx::OnTimerTimeout,
                           base::Unretained(this))),
                       delay_ms);
  }

  void KillTimer() override { timer_pending_ = false; }

  bool IsTimerPending() override { return timer_pending_; }

 private:
  bool timer_pending_ = false;
};

}  // namespace

// static
std::unique_ptr<MainMessageLoopExternalPump> MainMessageLoopExternalPump::Create() {
  return std::make_unique<MainMessageLoopExternalPumpQnx>();
}

}  // namespace client
