// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_PLATFORM_EVENT_SOURCE_H_
#define UI_OZONE_PLATFORM_QNX_QNX_PLATFORM_EVENT_SOURCE_H_

#include <screen/screen.h>

#include "base/component_export.h"
#include "base/memory/raw_ptr.h"
#include "ui/events/platform/platform_event_source.h"

// Phase 4, second substep: minimal QNX Screen event source.
// Owns the screen_context_t and polls for QNX Screen events using
// screen_get_event() with a short non-blocking timeout. Translates and
// dispatches events to registered PlatformEventDispatchers.
//
// Scope: compile-safe polling skeleton. Close/keyboard/pointer events are
// logged or partially translated. Full IME, display enumeration via events,
// and GPU producer input are deferred.
namespace ui {

class QnxWindowManager;

class COMPONENT_EXPORT(OZONE_BASE) QnxPlatformEventSource
    : public PlatformEventSource {
 public:
  // Returns the thread-local singleton, or nullptr if not yet created.
  static QnxPlatformEventSource* GetInstance();

  explicit QnxPlatformEventSource(screen_context_t ctx,
                                  QnxWindowManager* window_manager);
  ~QnxPlatformEventSource() override;

  QnxPlatformEventSource(const QnxPlatformEventSource&) = delete;
  QnxPlatformEventSource& operator=(const QnxPlatformEventSource&) = delete;

  // Start polling for Screen events. Called after the message pump is running.
  void Start();

  // Stop polling. Called during shutdown.
  void Stop();

  // Returns true if polling is active.
  bool IsRunning() const;

 protected:
  // PlatformEventSource override: dispatch a native event to registered
  // dispatchers. QNX Screen events are dispatched through this path.
  uint32_t DispatchEvent(PlatformEvent platform_event) override;

 private:
  // Callback invoked by the timer to poll for one Screen event.
  void OnTimerTick();

  // Translate a Screen event into a PlatformEvent and dispatch it.
  // Returns true if a meaningful event was dispatched.
  bool TranslateScreenEvent();

  // Log a Screen event with its type and key window properties for debugging.
  void DumpScreenEvent(int event_type, screen_window_t win);

  // QNX Screen context. Not wrapped in raw_ptr because screen_context_t is
  // an opaque pointer type (struct _screen_context*) and wrapping it in
  // raw_ptr triggers a build error due to incomplete-type issues.
  screen_context_t context_ = nullptr;
  raw_ptr<QnxWindowManager> window_manager_;

  // Reused screen_event_t handle for polling. Allocated once at construction.
  screen_event_t screen_event_ = nullptr;

  // Timer for periodic polling. Uses MessagePump scheduling.
  base::WeakPtrFactory<QnxPlatformEventSource> weak_factory_{this};
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_PLATFORM_EVENT_SOURCE_H_
