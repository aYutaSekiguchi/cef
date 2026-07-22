// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_WINDOW_H_
#define UI_OZONE_PLATFORM_QNX_QNX_WINDOW_H_

#include <optional>
#include <screen/screen.h>

#include "base/memory/raw_ptr.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_delegate.h"

namespace ui {

class QnxWindowManager;
class QnxScreenContext;

// QnxWindow wraps a QNX Screen window (screen_window_t) and implements the
// Chromium PlatformWindow interface.
//
// This class owns the visible screen_window_t. The stable gfx::AcceleratedWidget
// ID is allocated by QnxWindowManager and stored here. The raw screen_window_t
// pointer is never exposed outside the browser process.
//
// Phase 4 scope: create/destroy visible screen_window_t, set bounds/visibility,
// allocate stable widget ID. EGL display/composition surface setup is deferred
// to Phase 5.
class QnxWindow : public PlatformWindow {
 public:
  QnxWindow(PlatformWindowDelegate* delegate,
            QnxWindowManager* manager,
            QnxScreenContext* screen_context,
            const gfx::Rect& bounds);

  QnxWindow(const QnxWindow&) = delete;
  QnxWindow& operator=(const QnxWindow&) = delete;

  ~QnxWindow() override;

  // Returns the stable widget ID allocated by QnxWindowManager.
  gfx::AcceleratedWidget widget() const { return widget_; }

  // Returns the raw QNX Screen window handle (browser-process-only).
  screen_window_t screen_window() const { return screen_win_; }

  // PlatformWindow:
  void Show(bool inactive) override;
  void Hide() override;
  void Close() override;
  bool IsVisible() const override;
  void PrepareForShutdown() override;
  void SetBoundsInPixels(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInPixels() const override;
  void SetBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInDIP() const override;
  void SetTitle(const std::u16string& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  bool HasCapture() const override;
  void SetFullscreen(bool fullscreen, int64_t target_display_id) override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  PlatformWindowState GetPlatformWindowState() const override;
  void Activate() override;
  void Deactivate() override;
  void SetUseNativeFrame(bool use_native_frame) override;
  bool ShouldUseNativeFrame() const override;
  void SetCursor(scoped_refptr<PlatformCursor> cursor) override;
  void MoveCursorTo(const gfx::Point& location) override;
  void ConfineCursorToBounds(const gfx::Rect& bounds) override;
  void SetRestoredBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetRestoredBoundsInDIP() const override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override;
  void SizeConstraintsChanged() override;

 protected:
  PlatformWindowDelegate* delegate() { return delegate_; }

 private:
  enum class ActivationState { kUnknown, kActive, kInactive };

  // Create the underlying QNX Screen window.
  bool CreateScreenWindow(const gfx::Rect& bounds);

  // Restore window bounds from saved restored_bounds_.
  void RestoreWindowBounds();

  // Return the target QNX display bounds. QNX currently reports a device
  // scale factor of 1, so display DIP bounds and Screen pixel bounds match.
  gfx::Rect GetTargetDisplayBoundsInPixels(int64_t target_display_id,
                                           bool use_work_area) const;

  // Update the window_state_ and notify the delegate.
  void UpdateWindowState(PlatformWindowState new_window_state);

  // Destroy the QNX Screen window.
  void DestroyScreenWindow();

  raw_ptr<PlatformWindowDelegate> delegate_ = nullptr;
  raw_ptr<QnxWindowManager> manager_ = nullptr;
  raw_ptr<QnxScreenContext> screen_context_ = nullptr;

  screen_window_t screen_win_ = nullptr;
  gfx::AcceleratedWidget widget_ = gfx::kNullAcceleratedWidget;

  gfx::Rect bounds_;
  bool visible_ = false;
  bool capture_state_ = false;
  std::optional<gfx::Rect> restored_bounds_;
  PlatformWindowState window_state_ = PlatformWindowState::kUnknown;
  ActivationState activation_state_ = ActivationState::kUnknown;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_WINDOW_H_
