// Copyright 2026 The Chromium Authors
// Use of this code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Phase 4, second substep: minimal QNX Screen event source.
// Polls QNX Screen events via screen_get_event() with a short non-blocking
// timeout and dispatches them to registered PlatformEventDispatchers.
//
// Limitations (Phase 4 scope):
// - Uses a periodic timer rather than a dedicated polling thread.
// - Keyboard, pointer, and close events are logged or partially translated.
// - Full IME, display-enumeration events, multi-touch, and GPU input deferred.
// - Non-blocking short-timeout polling avoids busy loops.

#include "ui/ozone/platform/qnx/qnx_platform_event_source.h"

#include <errno.h>
#include <iomanip>
#include <string.h>

#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "ui/events/event.h"
#include "ui/events/ozone/events_ozone.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"

namespace ui {

namespace {

const char* SafeStrError(int error_number) {
  const char* message = strerror(error_number);
  return message ? message : "unknown error";
}

void LogScreenError(const char* operation, int error_number) {
  LOG(ERROR) << operation << " failed: errno=" << error_number << " ("
             << SafeStrError(error_number) << ")";
}

// Short timeout for screen_get_event() in nanoseconds.
// 16ms (~60 fps) balances responsiveness with CPU cost.
// This is the minimum polling granularity; events arriving between ticks
// are picked up on the next tick.
constexpr int64_t kScreenEventTimeoutNs = 16 * 1000 * 1000;  // 16 ms

// Polling interval for the timer. Match the event timeout for simplicity.
// A longer interval (e.g. 50ms) reduces CPU at the cost of input latency.
constexpr int64_t kPollingIntervalMs = 16;  // ~60 Hz

// Maximum length for a SCREEN_PROPERTY_NAME string retrieved from events.
constexpr size_t kMaxEventNameLen = 64;

// Event types handled in this Phase 4 skeleton.
constexpr int kHandledEventTypes[] = {
    SCREEN_EVENT_CLOSE,
    SCREEN_EVENT_KEYBOARD,
    SCREEN_EVENT_POINTER,
    SCREEN_EVENT_DISPLAY,
    SCREEN_EVENT_IDLE,
    SCREEN_EVENT_MTOUCH_TOUCH,
    SCREEN_EVENT_MTOUCH_MOVE,
    SCREEN_EVENT_MTOUCH_RELEASE,
};

// Returns a human-readable name for a SCREEN_EVENT_* type.
const char* ScreenEventTypeName(int type) {
  switch (type) {
    case SCREEN_EVENT_NONE:
      return "NONE";
    case SCREEN_EVENT_CREATE:
      return "CREATE";
    case SCREEN_EVENT_PROPERTY:
      return "PROPERTY";
    case SCREEN_EVENT_CLOSE:
      return "CLOSE";
    case SCREEN_EVENT_INPUT:
      return "INPUT";
    case SCREEN_EVENT_JOG:
      return "JOG";
    case SCREEN_EVENT_POINTER:
      return "POINTER";
    case SCREEN_EVENT_KEYBOARD:
      return "KEYBOARD";
    case SCREEN_EVENT_USER:
      return "USER";
    case SCREEN_EVENT_POST:
      return "POST";
    case SCREEN_EVENT_DISPLAY:
      return "DISPLAY";
    case SCREEN_EVENT_IDLE:
      return "IDLE";
    case SCREEN_EVENT_UNREALIZE:
      return "UNREALIZE";
    case SCREEN_EVENT_GAMEPAD:
      return "GAMEPAD";
    case SCREEN_EVENT_JOYSTICK:
      return "JOYSTICK";
    case SCREEN_EVENT_INPUT_CONTROL:
      return "INPUT_CONTROL";
    case SCREEN_EVENT_GESTURE:
      return "GESTURE";
    case SCREEN_EVENT_MANAGER:
      return "MANAGER";
    case SCREEN_EVENT_MTOUCH_PRETOUCH:
      return "MTOUCH_PRETOUCH";
    case SCREEN_EVENT_MTOUCH_TOUCH:
      return "MTOUCH_TOUCH";
    case SCREEN_EVENT_MTOUCH_MOVE:
      return "MTOUCH_MOVE";
    case SCREEN_EVENT_MTOUCH_RELEASE:
      return "MTOUCH_RELEASE";
    default:
      return "UNKNOWN";
  }
}

}  // namespace

// static
QnxPlatformEventSource* QnxPlatformEventSource::GetInstance() {
  return static_cast<QnxPlatformEventSource*>(PlatformEventSource::GetInstance());
}

QnxPlatformEventSource::QnxPlatformEventSource(
    screen_context_t ctx,
    QnxWindowManager* window_manager)
    : context_(ctx), window_manager_(window_manager) {
  // Create a reusable screen_event_t handle for polling.
  if (context_) {
    int rc = screen_create_event(&screen_event_);
    if (rc != 0) {
      LogScreenError("QnxPlatformEventSource: screen_create_event", errno);
      screen_event_ = nullptr;
    } else {
      DLOG(INFO) << "QnxPlatformEventSource: created (event=" << screen_event_
                 << ")";
    }
  }
}

QnxPlatformEventSource::~QnxPlatformEventSource() {
  Stop();
  if (screen_event_) {
    screen_destroy_event(screen_event_);
    DLOG(INFO) << "QnxPlatformEventSource: screen_event destroyed";
    screen_event_ = nullptr;
  }
}

void QnxPlatformEventSource::Start() {
  if (!context_ || !screen_event_) {
    LOG(ERROR) << "QnxPlatformEventSource::Start: no context or event handle";
    return;
  }

  DLOG(INFO) << "QnxPlatformEventSource: starting event polling";

  // Schedule the first poll. Subsequent polls are rescheduled from the
  // callback itself to avoid drift. GetCurrentDefault() is safe here because
  // InitializeUI() runs on the main UI thread after the task runner is set up.
  scoped_refptr<base::SequencedTaskRunner> task_runner =
      base::SequencedTaskRunner::GetCurrentDefault();
  if (!task_runner) {
    LOG(ERROR) << "QnxPlatformEventSource::Start: no task runner available";
    return;
  }
  task_runner->PostTask(FROM_HERE,
                       base::BindOnce(&QnxPlatformEventSource::OnTimerTick,
                                      weak_factory_.GetWeakPtr()));
}

void QnxPlatformEventSource::Stop() {
  weak_factory_.InvalidateWeakPtrs();
  DLOG(INFO) << "QnxPlatformEventSource: stopped";
}

bool QnxPlatformEventSource::IsRunning() const {
  return weak_factory_.HasWeakPtrs();
}

void QnxPlatformEventSource::OnTimerTick() {
  // Poll for one Screen event with a short non-blocking timeout.
  // screen_get_event returns 0 on success; -1 on error.
  if (!context_ || !screen_event_)
    return;

  int rc = screen_get_event(context_, screen_event_, kScreenEventTimeoutNs);
  if (rc < 0) {
    LogScreenError("QnxPlatformEventSource: screen_get_event", errno);
    // Still schedule next tick even on error.
  } else {
    // Translate and dispatch if we got an event. screen_get_event succeeds
    // even for SCREEN_EVENT_NONE (timeout); we check the event type.
    int event_type = SCREEN_EVENT_NONE;
    screen_get_event_property_iv(screen_event_, SCREEN_PROPERTY_TYPE,
                                &event_type);

    if (event_type != SCREEN_EVENT_NONE) {
      TranslateScreenEvent();
    }
  }

  // Schedule the next poll if not stopped.
  if (weak_factory_.HasWeakPtrs()) {
    scoped_refptr<base::SequencedTaskRunner> task_runner =
        base::SequencedTaskRunner::GetCurrentDefault();
    if (task_runner) {
      task_runner->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&QnxPlatformEventSource::OnTimerTick,
                         weak_factory_.GetWeakPtr()),
          base::Milliseconds(kPollingIntervalMs));
    }  // if (task_runner)
  }  // if (weak_factory_.HasWeakPtrs())
}

uint32_t QnxPlatformEventSource::DispatchEvent(PlatformEvent platform_event) {
  // Delegate to the base class to notify observers and dispatch to
  // registered dispatchers.
  return PlatformEventSource::DispatchEvent(platform_event);
}

bool QnxPlatformEventSource::TranslateScreenEvent() {
  int event_type = SCREEN_EVENT_NONE;
  screen_get_event_property_iv(screen_event_, SCREEN_PROPERTY_TYPE,
                              &event_type);

  // Retrieve the source window, if any.
  screen_window_t win = nullptr;
  screen_get_event_property_pv(screen_event_, SCREEN_PROPERTY_WINDOW,
                              reinterpret_cast<void**>(&win));

  // Log all events for Phase 4 debugging; this helps trace input behavior.
  if (VLOG_IS_ON(1)) {
    DumpScreenEvent(event_type, win);
  }

  // Phase 4 conservative approach: only fully dispatch a subset of events.
  // Events that are not yet handled are logged but silently dropped.
  switch (event_type) {
    case SCREEN_EVENT_CLOSE: {
      // Window close: find the QnxWindow and notify its delegate.
      // QnxWindowManager maps screen_window_t -> QnxWindow.
      if (win && window_manager_) {
        // Scan windows to find the one matching this screen_window_t.
        // QnxWidgetRecord.screen_win holds the raw pointer.
        // This is a simplified close handler for Phase 4 smoke.
        DLOG(INFO) << "QnxPlatformEventSource: SCREEN_EVENT_CLOSE "
                      "for window="
                   << win;
        // The actual close propagation will be implemented in Phase 4
        // subsequent substeps when window-to-widget mapping is extended.
      }
      break;
    }

    case SCREEN_EVENT_KEYBOARD: {
      // Phase 4: keyboard events are logged but not yet dispatched.
      // Full keyboard translation requires KeyboardLayoutEngine and IME wiring.
      int flags = 0;
      int sym = 0;
      screen_get_event_property_iv(screen_event_, SCREEN_PROPERTY_FLAGS, &flags);
      screen_get_event_property_iv(screen_event_, SCREEN_PROPERTY_SYM, &sym);
      DLOG(INFO) << "QnxPlatformEventSource: KEYBOARD sym=0x" << std::hex << sym
                 << " flags=0x" << flags << std::dec;
      // TODO(qnx): Translate to ui::KeyEvent and dispatch.
      break;
    }

    case SCREEN_EVENT_POINTER: {
      // Phase 4: pointer events are logged but not yet dispatched.
      // Full pointer translation requires MouseEvent construction.
      int buttons = 0;
      int pos[2] = {0, 0};
      screen_get_event_property_iv(screen_event_, SCREEN_PROPERTY_BUTTONS,
                                  &buttons);
      screen_get_event_property_iv(screen_event_, SCREEN_PROPERTY_POSITION, pos);
      DLOG(INFO) << "QnxPlatformEventSource: POINTER pos=(" << pos[0] << ","
                 << pos[1] << ") buttons=0x" << std::hex << buttons << std::dec;
      // TODO(qnx): Translate to ui::MouseEvent and dispatch.
      break;
    }

    case SCREEN_EVENT_MTOUCH_TOUCH:
    case SCREEN_EVENT_MTOUCH_MOVE:
    case SCREEN_EVENT_MTOUCH_RELEASE: {
      DLOG(INFO) << "QnxPlatformEventSource: MTOUCH event type="
                 << ScreenEventTypeName(event_type);
      // TODO(qnx): Translate to ui::TouchEvent and dispatch.
      break;
    }

    case SCREEN_EVENT_DISPLAY: {
      DLOG(INFO) << "QnxPlatformEventSource: DISPLAY event";
      // TODO(qnx): Trigger display re-enumeration in QnxScreen.
      break;
    }

    case SCREEN_EVENT_IDLE: {
      DLOG(INFO) << "QnxPlatformEventSource: IDLE event";
      // TODO(qnx): Update idle state tracking.
      break;
    }

    case SCREEN_EVENT_NONE:
      // Timeout: no event to process.
      break;

    default:
      DLOG(INFO) << "QnxPlatformEventSource: unhandled event type="
                 << ScreenEventTypeName(event_type);
      break;
  }

  return true;  // Event was processed (logged or dispatched).
}

void QnxPlatformEventSource::DumpScreenEvent(int event_type,
                                            screen_window_t win) {
  // Retrieve additional event properties for verbose logging.
  char name_buf[kMaxEventNameLen] = {0};
  int rc = screen_get_event_property_cv(screen_event_, SCREEN_PROPERTY_NAME,
                                        sizeof(name_buf), name_buf);
  if (rc != 0) {
    name_buf[0] = '\0';
  } else if (strlen(name_buf) >= kMaxEventNameLen) {
    // Ensure null termination if the Screen API fills the buffer completely.
    name_buf[kMaxEventNameLen - 1] = '\0';
  }

  long long timestamp_ns = 0;
  screen_get_event_property_llv(screen_event_, SCREEN_PROPERTY_TIMESTAMP,
                                &timestamp_ns);

  VLOG(1) << "Screen event: type=" << ScreenEventTypeName(event_type)
          << " (" << event_type << ") window=" << win
          << " name='" << (name_buf[0] ? name_buf : "(null)") << "' ts_ns=" << timestamp_ns;
}

}  // namespace ui
