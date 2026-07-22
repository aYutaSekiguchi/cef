// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/qnx/qnx_screen.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/logging.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/platform/qnx/qnx_screen_context.h"
#include "ui/ozone/platform/qnx/qnx_window.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"

using display::Display;
using display::DisplayList;

namespace ui {

namespace {

// Keep a usable display only when QNX Screen enumeration is unavailable.
constexpr gfx::Size kFallbackDisplaySize(1024, 768);
constexpr float kDefaultDevicePixelRatio = 1.0f;

constexpr int64_t kFallbackDisplayId = 1;

}  // namespace

QnxScreen::QnxScreen(QnxScreenContext* screen_context,
                     QnxWindowManager* window_manager)
    : screen_context_(*screen_context), window_manager_(*window_manager) {
  CreateDisplayList();
}

QnxScreen::~QnxScreen() = default;

void QnxScreen::CreateDisplayList() {
  screen_context_t context = screen_context_->context();
  int display_count = 0;
  const int display_count_result = screen_get_context_property_iv(
      context, SCREEN_PROPERTY_DISPLAY_COUNT, &display_count);
  if (display_count_result == 0 && display_count > 0) {
    std::vector<void*> display_handles(display_count);
    if (screen_get_context_property_pv(context, SCREEN_PROPERTY_DISPLAYS,
                                       display_handles.data()) == 0) {
      for (void* display_handle : display_handles) {
        screen_display_t screen_display =
            static_cast<screen_display_t>(display_handle);
        int attached = 1;
        if (screen_get_display_property_iv(
                screen_display, SCREEN_PROPERTY_ATTACHED, &attached) == 0 &&
            !attached) {
          continue;
        }

        int position[2] = {};
        int size[2] = {};
        if (screen_get_display_property_iv(
                screen_display, SCREEN_PROPERTY_POSITION, position) != 0 ||
            screen_get_display_property_iv(screen_display, SCREEN_PROPERTY_SIZE,
                                           size) != 0) {
          PLOG(WARNING) << "QnxScreen: failed to query display geometry";
          continue;
        }
        if (size[0] <= 0 || size[1] <= 0) {
          LOG(WARNING) << "QnxScreen: ignoring invalid display size="
                       << gfx::Size(size[0], size[1]).ToString();
          continue;
        }

        int screen_display_id = 0;
        if (screen_get_display_property_iv(screen_display, SCREEN_PROPERTY_ID,
                                           &screen_display_id) != 0) {
          screen_display_id =
              static_cast<int>(display_list_.displays().size()) + 1;
        }

        const gfx::Rect display_bounds(position[0], position[1], size[0],
                                       size[1]);
        Display display(screen_display_id, display_bounds);
        display.set_device_scale_factor(kDefaultDevicePixelRatio);
        display_list_.AddDisplay(display, display_list_.displays().empty()
                                              ? DisplayList::Type::PRIMARY
                                              : DisplayList::Type::NOT_PRIMARY);
        LOG(INFO) << "QnxScreen: detected display id=" << screen_display_id
                  << " bounds=" << display_bounds.ToString();
      }
    } else {
      PLOG(WARNING) << "QnxScreen: failed to enumerate displays";
    }
  } else if (display_count_result != 0) {
    PLOG(WARNING) << "QnxScreen: failed to query display count";
  } else {
    LOG(WARNING) << "QnxScreen: no displays reported by QNX Screen";
  }

  if (!display_list_.displays().empty()) {
    return;
  }

  const gfx::Rect display_bounds(gfx::Point(), kFallbackDisplaySize);
  Display display(kFallbackDisplayId, display_bounds);
  display.set_device_scale_factor(kDefaultDevicePixelRatio);
  display_list_.AddDisplay(display, DisplayList::Type::PRIMARY);
  LOG(WARNING) << "QnxScreen: using fallback display bounds="
               << display_bounds.ToString();
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
