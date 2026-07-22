// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_SCREEN_H_
#define UI_OZONE_PLATFORM_QNX_QNX_SCREEN_H_

#include <vector>

#include "base/memory/raw_ref.h"
#include "ui/display/display_list.h"
#include "ui/gfx/geometry/point.h"
#include "ui/ozone/public/platform_screen.h"

namespace ui {

class QnxScreenContext;
class QnxWindowManager;

// QNX PlatformScreen implementation backed by QNX Screen display enumeration.
class QnxScreen : public PlatformScreen {
 public:
  QnxScreen(QnxScreenContext* screen_context, QnxWindowManager* window_manager);
  ~QnxScreen() override;

  // Overridden from ui::PlatformScreen:
  const std::vector<display::Display>& GetAllDisplays() const override;
  display::Display GetPrimaryDisplay() const override;
  display::Display GetDisplayForAcceleratedWidget(
      gfx::AcceleratedWidget widget) const override;
  gfx::Point GetCursorScreenPoint() const override;
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point) const override;
  display::Display GetDisplayNearestPoint(
      const gfx::Point& point) const override;
  display::Display GetDisplayMatching(
      const gfx::Rect& match_rect) const override;
  bool IsScreenSaverActive() const override;
  base::TimeDelta CalculateIdleTime() const override;
  void AddObserver(display::DisplayObserver* observer) override;
  void RemoveObserver(display::DisplayObserver* observer) override;
  bool IsHeadless() const override;

 private:
  void CreateDisplayList();

  const raw_ref<QnxScreenContext> screen_context_;
  const raw_ref<QnxWindowManager> window_manager_;
  display::DisplayList display_list_;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_SCREEN_H_
