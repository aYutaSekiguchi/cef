// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/qnx/qnx_window.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

#include <errno.h>
#include <string.h>

#include "base/logging.h"
#include "base/notimplemented.h"
#include "base/notreached.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/cursor/platform_cursor.h"
#include "ui/display/screen.h"
#include "ui/display/types/display_constants.h"
#include "ui/ozone/platform/qnx/qnx_screen_context.h"
#include "ui/ozone/platform/qnx/qnx_window_manager.h"

namespace ui {

namespace {

// QNX Screen usage flags for the Chromium browser window.
// SCREEN_USAGE_OPENGL_ES2 enables GL rendering on this window.
constexpr int kScreenUsageFlags = SCREEN_USAGE_OPENGL_ES2;

// QNX Screen sensitivity values for input capture.
// SCREEN_SENSITIVITY_ALWAYS = 1 means the window receives input regardless
// of focus. SCREEN_SENSITIVITY_NO_FOCUS = 3 means no input.
constexpr int kSensitivityCapture = SCREEN_SENSITIVITY_ALWAYS;
constexpr int kSensitivityRelease = SCREEN_SENSITIVITY_NO_FOCUS;

}  // namespace

QnxWindow::QnxWindow(PlatformWindowDelegate* delegate,
                     QnxWindowManager* manager,
                     QnxScreenContext* screen_context,
                     const gfx::Rect& bounds)
    : delegate_(delegate),
      manager_(manager),
      screen_context_(screen_context),
      bounds_(bounds) {
  if (!screen_context_->is_valid()) {
    LOG(ERROR) << "QnxWindow: cannot create window: invalid Screen context";
    return;
  }

  if (!CreateScreenWindow(bounds)) {
    LOG(ERROR) << "QnxWindow: CreateScreenWindow failed";
    return;
  }

  // Register with the window manager. This allocates the stable widget ID.
  widget_ = manager_->AddWindow(this);
  delegate_->OnAcceleratedWidgetAvailable(widget_);

  QNX_GPU_TRACE_LOG(INFO) << "QnxWindow: created widget=" << widget_
             << " screen_win=" << screen_win_ << " bounds=" << bounds_.ToString();
}

QnxWindow::~QnxWindow() {
  DestroyScreenWindow();
  if (widget_ != gfx::kNullAcceleratedWidget) {
    manager_->RemoveWindow(widget_, this);
  }
}

bool QnxWindow::CreateScreenWindow(const gfx::Rect& bounds) {
  // Step 1: Create the raw screen window.
  int rc = screen_create_window(&screen_win_, screen_context_->context());
  if (rc != 0) {
    PLOG(ERROR) << "QnxWindow: screen_create_window failed";
    return false;
  }

  // Step 2: Set window size (in pixels).
  int size[2] = {bounds.width(), bounds.height()};
  rc = screen_set_window_property_iv(screen_win_, SCREEN_PROPERTY_SIZE, size);
  if (rc != 0) {
    PLOG(ERROR) << "QnxWindow: screen_set SCREEN_PROPERTY_SIZE failed";
    screen_destroy_window(screen_win_);
    screen_win_ = nullptr;
    return false;
  }

  // Step 3: Set window position.
  int pos[2] = {bounds.x(), bounds.y()};
  rc = screen_set_window_property_iv(screen_win_, SCREEN_PROPERTY_POSITION, pos);
  if (rc != 0) {
    PLOG(WARNING) << "QnxWindow: screen_set SCREEN_PROPERTY_POSITION failed";
    // Non-fatal; default to 0,0.
  }

  // Step 4: Set usage flags (enable GLES2 rendering on this window).
  rc = screen_set_window_property_iv(screen_win_, SCREEN_PROPERTY_USAGE,
                                     &kScreenUsageFlags);
  if (rc != 0) {
    PLOG(ERROR) << "QnxWindow: screen_set SCREEN_PROPERTY_USAGE failed";
    screen_destroy_window(screen_win_);
    screen_win_ = nullptr;
    return false;
  }

  // Step 5: Set initial visibility — visible from creation.
  int visible = 1;
  rc = screen_set_window_property_iv(screen_win_, SCREEN_PROPERTY_VISIBLE,
                                    &visible);
  if (rc != 0) {
    PLOG(WARNING) << "QnxWindow: screen_set SCREEN_PROPERTY_VISIBLE failed";
    // Non-fatal; window may still be visible by default.
  }

  // Step 6: Create the window buffer(s). SCREEN_USAGE_OPENGL_ES2 requires at
  // least one buffer for rendering.
  rc = screen_create_window_buffers(screen_win_, 1);
  if (rc != 0) {
    PLOG(ERROR)
        << "QnxWindow: screen_create_window_buffers failed";
    screen_destroy_window(screen_win_);
    screen_win_ = nullptr;
    return false;
  }

  return true;
}

void QnxWindow::DestroyScreenWindow() {
  if (screen_win_) {
    screen_destroy_window(screen_win_);
    QNX_GPU_TRACE_LOG(INFO) << "QnxWindow: screen_destroy_window done";
    screen_win_ = nullptr;
  }
  capture_state_ = false;
}

void QnxWindow::Show(bool inactive) {
  if (!screen_win_)
    return;
  int visible = 1;
  screen_set_window_property_iv(screen_win_, SCREEN_PROPERTY_VISIBLE, &visible);
  visible_ = true;
  // Notify delegate. Aura/WindowTreeHost uses OnActivationChanged.
  Activate();
}

void QnxWindow::Hide() {
  if (!screen_win_)
    return;
  int visible = 0;
  screen_set_window_property_iv(screen_win_, SCREEN_PROPERTY_VISIBLE, &visible);
  visible_ = false;
}

void QnxWindow::Close() {
  DestroyScreenWindow();
  delegate_->OnClosed();
}

bool QnxWindow::IsVisible() const {
  return visible_;
}

void QnxWindow::PrepareForShutdown() {}

void QnxWindow::SetBoundsInPixels(const gfx::Rect& bounds) {
  const gfx::Rect old_bounds = bounds_;
  bounds_ = bounds;
  if (!screen_win_)
    return;

  int size[2] = {bounds.width(), bounds.height()};
  int rc = screen_set_window_property_iv(screen_win_, SCREEN_PROPERTY_SIZE, size);
  if (rc != 0) {
    PLOG(WARNING) << "QnxWindow: resize failed";
  }

  int pos[2] = {bounds.x(), bounds.y()};
  rc = screen_set_window_property_iv(screen_win_, SCREEN_PROPERTY_POSITION, pos);
  if (rc != 0) {
    PLOG(WARNING) << "QnxWindow: reposition failed";
  }

  bool origin_changed = old_bounds.origin() != bounds.origin();
  delegate_->OnBoundsChanged({origin_changed});
}

gfx::Rect QnxWindow::GetBoundsInPixels() const {
  return bounds_;
}

void QnxWindow::SetBoundsInDIP(const gfx::Rect& bounds) {
  SetBoundsInPixels(delegate_->ConvertRectToPixels(bounds));
}

gfx::Rect QnxWindow::GetBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(bounds_);
}

void QnxWindow::SetTitle(const std::u16string& title) {
  if (!screen_win_)
    return;
  // QNX Screen uses SCREEN_PROPERTY_ID_STRING for window title/name.
  std::string title_utf8 = base::UTF16ToUTF8(title);
  int rc = screen_set_window_property_cv(
      screen_win_, SCREEN_PROPERTY_ID_STRING, title_utf8.size(),
      title_utf8.c_str());
  if (rc != 0) {
    PLOG(WARNING) << "QnxWindow: screen_set SCREEN_PROPERTY_ID_STRING failed";
  }
}

void QnxWindow::SetCapture() {
  // QNX Screen uses SCREEN_PROPERTY_SENSITIVITY for input capture.
  if (!screen_win_)
    return;
  int rc = screen_set_window_property_iv(
      screen_win_, SCREEN_PROPERTY_SENSITIVITY, &kSensitivityCapture);
  if (rc != 0) {
    PLOG(WARNING) << "QnxWindow: SetCapture failed";
    return;
  }
  capture_state_ = true;
}

void QnxWindow::ReleaseCapture() {
  if (!screen_win_)
    return;
  int rc = screen_set_window_property_iv(
      screen_win_, SCREEN_PROPERTY_SENSITIVITY, &kSensitivityRelease);
  if (rc != 0) {
    PLOG(WARNING) << "QnxWindow: ReleaseCapture failed";
    return;
  }
  capture_state_ = false;
}

bool QnxWindow::HasCapture() const {
  return capture_state_;
}

void QnxWindow::SetFullscreen(bool fullscreen, int64_t target_display_id) {
  if (fullscreen) {
    if (window_state_ != PlatformWindowState::kMaximized &&
        window_state_ != PlatformWindowState::kFullScreen) {
      restored_bounds_ = bounds_;
    }
    SetBoundsInPixels(GetTargetDisplayBoundsInPixels(
        target_display_id, /*use_work_area=*/false));
    UpdateWindowState(PlatformWindowState::kFullScreen);
  } else {
    if (window_state_ != PlatformWindowState::kFullScreen)
      return;
    RestoreWindowBounds();
    UpdateWindowState(PlatformWindowState::kNormal);
  }
}

void QnxWindow::Maximize() {
  if (window_state_ != PlatformWindowState::kMaximized &&
      window_state_ != PlatformWindowState::kFullScreen) {
    restored_bounds_ = bounds_;
    SetBoundsInPixels(GetTargetDisplayBoundsInPixels(
        display::kInvalidDisplayId, /*use_work_area=*/true));
    UpdateWindowState(PlatformWindowState::kMaximized);
  }
}

void QnxWindow::Minimize() {
  if (window_state_ != PlatformWindowState::kMinimized) {
    if (window_state_ == PlatformWindowState::kMaximized ||
        window_state_ == PlatformWindowState::kFullScreen) {
      RestoreWindowBounds();
    }
    // QNX: minimize means hide the window.
    Hide();
    UpdateWindowState(PlatformWindowState::kMinimized);
    Deactivate();
  }
}

void QnxWindow::Restore() {
  if (window_state_ != PlatformWindowState::kNormal) {
    RestoreWindowBounds();
    UpdateWindowState(PlatformWindowState::kNormal);
  }
}

PlatformWindowState QnxWindow::GetPlatformWindowState() const {
  return window_state_;
}

void QnxWindow::Activate() {
  if (activation_state_ != ActivationState::kActive) {
    activation_state_ = ActivationState::kActive;
    delegate_->OnActivationChanged(/*active=*/true);
  }
}

void QnxWindow::Deactivate() {
  if (activation_state_ != ActivationState::kInactive) {
    activation_state_ = ActivationState::kInactive;
    delegate_->OnActivationChanged(/*active=*/false);
  }
}

void QnxWindow::SetUseNativeFrame(bool use_native_frame) {}

bool QnxWindow::ShouldUseNativeFrame() const {
  return false;
}

void QnxWindow::SetCursor(scoped_refptr<PlatformCursor> cursor) {}

void QnxWindow::MoveCursorTo(const gfx::Point& location) {}

void QnxWindow::ConfineCursorToBounds(const gfx::Rect& bounds) {}

void QnxWindow::SetRestoredBoundsInDIP(const gfx::Rect& bounds) {
  restored_bounds_ = delegate_->ConvertRectToPixels(bounds);
}

gfx::Rect QnxWindow::GetRestoredBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(restored_bounds_.value_or(bounds_));
}

void QnxWindow::SetWindowIcons(const gfx::ImageSkia& window_icon,
                                const gfx::ImageSkia& app_icon) {}

void QnxWindow::SizeConstraintsChanged() {}

void QnxWindow::RestoreWindowBounds() {
  if (restored_bounds_) {
    gfx::Rect restored = *restored_bounds_;
    restored_bounds_.reset();
    SetBoundsInPixels(restored);
  }
}

gfx::Rect QnxWindow::GetTargetDisplayBoundsInPixels(
    int64_t target_display_id,
    bool use_work_area) const {
  display::Screen* screen = display::Screen::Get();
  if (!screen)
    return bounds_;

  display::Display target = screen->GetDisplayMatching(bounds_);
  if (target_display_id != display::kInvalidDisplayId) {
    for (const display::Display& candidate : screen->GetAllDisplays()) {
      if (candidate.id() == target_display_id) {
        target = candidate;
        break;
      }
    }
  }
  return use_work_area ? target.work_area() : target.bounds();
}

void QnxWindow::UpdateWindowState(PlatformWindowState new_window_state) {
  if (window_state_ == new_window_state)
    return;
  auto old_state = window_state_;
  window_state_ = new_window_state;
  delegate_->OnWindowStateChanged(old_state, new_window_state);
}

}  // namespace ui
