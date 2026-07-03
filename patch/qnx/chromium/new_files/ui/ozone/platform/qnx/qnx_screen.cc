// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/qnx/qnx_screen.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/platform/qnx/qnx_window.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"

using display::Display;
using display::DisplayList;

namespace ui {

namespace {

// Default QNX display dimensions for the Phase 4 skeleton.
// QNX Screen runtime should override these with the actual display size
// once the event source is wired (Phase 4 substep).
constexpr gfx::Size kDefaultDisplaySize(1024, 768);
constexpr float kDefaultDevicePixelRatio = 1.0f;

// Stable display ID for the primary display in the Phase 4 skeleton.
// Real display IDs from QNX Screen enumeration will be used in a later phase.
constexpr int64_t kQnxPrimaryDisplayId = 1;

}  // namespace

QnxScreen::QnxScreen(QnxWindowManager* window_manager)
    : window_manager_(*window_manager) {
  CreateDisplayList();
}

QnxScreen::~QnxScreen() = default;

void QnxScreen::CreateDisplayList() {
  // Phase 4: create a single primary display with default size.
  // Real display enumeration from QNX Screen will be added in a later
  // Phase 4 substep once the event source is wired.
  gfx::Rect display_bounds(gfx::Point(), kDefaultDisplaySize);
  Display display(kQnxPrimaryDisplayId, display_bounds);
  display.set_device_scale_factor(kDefaultDevicePixelRatio);
  display_list_.AddDisplay(display, DisplayList::Type::PRIMARY);
}

const std::vector<Display>& QnxScreen::GetAllDisplays() const {
  return display_list_.displays();
}

Display QnxScreen::GetPrimaryDisplay() const {
  auto iter = display_list_.GetPrimaryDisplayIterator();
  CHECK(iter != display_list_.displays().end());
  return *iter;
}

Display QnxScreen::GetDisplayForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  if (widget == gfx::kNullAcceleratedWidget)
    return GetPrimaryDisplay();

  if (const QnxWindow* window = window_manager_->GetWindow(widget)) {
    gfx::Rect bounds = window->GetBoundsInPixels();
    return GetDisplayMatching(bounds);
  }
  return GetPrimaryDisplay();
}

gfx::Point QnxScreen::GetCursorScreenPoint() const {
  // Phase 4: no input/window-mouse tracking yet.
  // Return the center of the primary display.
  gfx::Rect primary = GetPrimaryDisplay().bounds();
  return gfx::Point(primary.CenterPoint());
}

gfx::AcceleratedWidget QnxScreen::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) const {
  return window_manager_->GetAcceleratedWidgetAtScreenPoint(point);
}

Display QnxScreen::GetDisplayNearestPoint(const gfx::Point& point) const {
  const auto& displays = GetAllDisplays();
  if (displays.empty())
    return GetPrimaryDisplay();

  // Find the display whose bounds contain the point.
  for (const Display& display : displays) {
    if (display.bounds().Contains(point))
      return display;
  }
  // If no display contains the point, return the primary display.
  return GetPrimaryDisplay();
}

Display QnxScreen::GetDisplayMatching(const gfx::Rect& match_rect) const {
  const auto& displays = GetAllDisplays();
  if (displays.empty())
    return GetPrimaryDisplay();

  // Find the display with the largest intersection area.
  const Display* best = &displays[0];
  int best_area = 0;
  for (const Display& display : displays) {
    gfx::Rect intersection = gfx::IntersectRects(display.bounds(), match_rect);
    int area = intersection.size().GetArea();
    if (area > best_area) {
      best_area = area;
      best = &display;
    }
  }
  return *best;
}

bool QnxScreen::IsScreenSaverActive() const {
  // Phase 4: no screen saver tracking.
  return false;
}

base::TimeDelta QnxScreen::CalculateIdleTime() const {
  // Phase 4: no idle tracking.
  return base::Seconds(0);
}

void QnxScreen::AddObserver(display::DisplayObserver* observer) {
  display_list_.AddObserver(observer);
}

void QnxScreen::RemoveObserver(display::DisplayObserver* observer) {
  display_list_.RemoveObserver(observer);
}

bool QnxScreen::IsHeadless() const {
  // Not headless: QNX Screen is a real display backend.
  return false;
}

}  // namespace ui
