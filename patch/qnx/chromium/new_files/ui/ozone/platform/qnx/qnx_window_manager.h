// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_WINDOW_MANAGER_H_
#define UI_OZONE_PLATFORM_QNX_QNX_WINDOW_MANAGER_H_

#include <stdint.h>

#include <map>
#include <vector>

#include "base/containers/id_map.h"
#include "base/threading/thread_checker.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_ui_types.h"

namespace ui {

class QnxWindow;

// Per-widget record tracking GPU attachment and generation.
// This is the stable widget ID model described in the Phase 2 design:
// gfx::AcceleratedWidget is a uint32_t, not a screen_window_t pointer.
struct QnxWidgetRecord {
  // Stable numeric widget ID (1..N), managed entirely in the browser process.
  gfx::AcceleratedWidget id = gfx::kNullAcceleratedWidget;

  // Incremented each time the GPU process reconnects.
  uint32_t generation = 0;

  // Last confirmed pixel size of the widget.
  gfx::Size size;

  // Raw screen_window_t pointer — browser-process-only, never sent over IPC.
  // Set to nullptr once the window has been destroyed.
  void* screen_win = nullptr;

  // True when the GPU process is currently connected and has attached to this
  // widget.
  bool gpu_attached = false;

  // GPU process PID when attached, 0 when detached.
  int gpu_pid = 0;

  // Pointer back to the QnxWindow for this widget (non-owning).
  QnxWindow* window = nullptr;
};

class QnxWindowManager {
 public:
  QnxWindowManager();
  ~QnxWindowManager();

  static QnxWindowManager* GetInstance();

  QnxWindowManager(const QnxWindowManager&) = delete;
  QnxWindowManager& operator=(const QnxWindowManager&) = delete;

  // Register a new QnxWindow. Returns the stable widget ID.
  gfx::AcceleratedWidget AddWindow(QnxWindow* window);

  // Unregister a window.
  void RemoveWindow(gfx::AcceleratedWidget widget, QnxWindow* window);

  // Look up a window by widget ID.
  QnxWindow* GetWindow(gfx::AcceleratedWidget widget);

  // Return the widget record by widget ID.
  const QnxWidgetRecord* GetWidgetRecord(gfx::AcceleratedWidget widget) const;

  // Return a widget at a given screen point (if any).
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point);

  // Mark a widget as GPU-attached or GPU-detached.
  void SetGpuAttached(gfx::AcceleratedWidget widget,
                      bool attached,
                      int gpu_pid);

  // Increment the generation for a widget (called on GPU reconnect).
  void IncrementGeneration(gfx::AcceleratedWidget widget);

  // Update the stored size for a widget.
  void UpdateWidgetSize(gfx::AcceleratedWidget widget, const gfx::Size& size);

  // Update the screen_win pointer for a widget (set after window creation).
  void SetScreenWindow(gfx::AcceleratedWidget widget, void* screen_win);

  // Increment generation and mark GPU-detached for ALL widgets.
  // Called when the GPU channel is destroyed so any stale frames
  // from the old GPU process are discarded.
  void DetachAllWidgets();

  // Return a snapshot of all widget records for iteration.
  // Useful for GPU attach/detach operations that need to traverse
  // all widgets atomically.  The returned vector is a copy; records_
  // can be modified during iteration.
  std::vector<QnxWidgetRecord> GetWidgetRecordsForTestingOrGpuAttach() const;

 private:
  // Singleton instance pointer. Set in constructor, cleared in destructor.
  // This is safe because there is only one OzonePlatform per process and
  // QnxWindowManager is created/destroyed with it.
  static QnxWindowManager* instance_;

  base::IDMap<QnxWindow*> windows_;
  std::map<gfx::AcceleratedWidget, QnxWidgetRecord> records_;
  base::ThreadChecker thread_checker_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_WINDOW_MANAGER_H_
