// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/qnx/qnx_window_manager.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

#include "base/check.h"
#include "base/logging.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/platform/qnx/qnx_window.h"

namespace ui {

// static
QnxWindowManager* QnxWindowManager::instance_ = nullptr;

// static
QnxWindowManager* QnxWindowManager::GetInstance() {
  return instance_;
}

QnxWindowManager::QnxWindowManager() {
  // There is only one QnxWindowManager per OzonePlatform instance.
  // It is constructed in InitializeUI() and destroyed when the platform exits.
  instance_ = this;
}

QnxWindowManager::~QnxWindowManager() {
  DCHECK(thread_checker_.CalledOnValidThread());
  instance_ = nullptr;
}

gfx::AcceleratedWidget QnxWindowManager::AddWindow(QnxWindow* window) {
  DCHECK(thread_checker_.CalledOnValidThread());
  gfx::AcceleratedWidget widget = windows_.Add(window);

  QnxWidgetRecord record;
  record.id = widget;
  record.window = window;
  records_[widget] = record;

  QNX_GPU_TRACE_LOG(INFO) << "QnxWindowManager: AddWindow widget=" << widget;
  return widget;
}

void QnxWindowManager::RemoveWindow(gfx::AcceleratedWidget widget,
                                    QnxWindow* window) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(window, windows_.Lookup(widget));
  windows_.Remove(widget);
  records_.erase(widget);
  QNX_GPU_TRACE_LOG(INFO) << "QnxWindowManager: RemoveWindow widget=" << widget;
}

QnxWindow* QnxWindowManager::GetWindow(gfx::AcceleratedWidget widget) {
  return windows_.Lookup(widget);
}

const QnxWidgetRecord* QnxWindowManager::GetWidgetRecord(
    gfx::AcceleratedWidget widget) const {
  auto it = records_.find(widget);
  if (it == records_.end())
    return nullptr;
  return &it->second;
}

gfx::AcceleratedWidget QnxWindowManager::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) {
  for (base::IDMap<QnxWindow*>::const_iterator it(&windows_);
       !it.IsAtEnd(); it.Advance()) {
    const QnxWindow* window = it.GetCurrentValue();
    gfx::Rect bounds = window->GetBoundsInPixels();
    if (bounds.Contains(point)) {
      return window->widget();
    }
  }
  return gfx::kNullAcceleratedWidget;
}

void QnxWindowManager::SetGpuAttached(gfx::AcceleratedWidget widget,
                                      bool attached,
                                      int gpu_pid) {
  auto it = records_.find(widget);
  if (it == records_.end())
    return;
  it->second.gpu_attached = attached;
  it->second.gpu_pid = attached ? gpu_pid : 0;
  QNX_GPU_TRACE_LOG(INFO) << "QnxWindowManager: SetGpuAttached widget=" << widget
             << " attached=" << attached;
}

void QnxWindowManager::IncrementGeneration(gfx::AcceleratedWidget widget) {
  auto it = records_.find(widget);
  if (it == records_.end())
    return;
  it->second.generation++;
  QNX_GPU_TRACE_LOG(INFO) << "QnxWindowManager: IncrementGeneration widget=" << widget
             << " new_gen=" << it->second.generation;
}

void QnxWindowManager::UpdateWidgetSize(gfx::AcceleratedWidget widget,
                                        const gfx::Size& size) {
  auto it = records_.find(widget);
  if (it == records_.end())
    return;
  it->second.size = size;
}

void QnxWindowManager::SetScreenWindow(gfx::AcceleratedWidget widget,
                                       void* screen_win) {
  auto it = records_.find(widget);
  if (it == records_.end())
    return;
  it->second.screen_win = screen_win;
}

void QnxWindowManager::DetachAllWidgets() {
  auto it = records_.begin();
  while (it != records_.end()) {
    if (it->second.gpu_attached) {
      uint32_t old_gen = it->second.generation;
      it->second.gpu_attached = false;
      it->second.gpu_pid = 0;
      it->second.generation++;
      QNX_GPU_TRACE_LOG(INFO) << "QnxWindowManager::DetachAllWidgets: widget="
                 << it->first << " gen " << old_gen << " -> "
                 << it->second.generation << " (GPU detached)";
    }
    ++it;
  }
}

std::vector<QnxWidgetRecord> QnxWindowManager::
    GetWidgetRecordsForTestingOrGpuAttach() const {
  std::vector<QnxWidgetRecord> result;
  result.reserve(records_.size());
  for (const auto& [widget, record] : records_) {
    result.push_back(record);
  }
  return result;
}

}  // namespace ui
