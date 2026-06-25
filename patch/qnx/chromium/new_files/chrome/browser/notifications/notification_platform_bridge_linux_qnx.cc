// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_platform_bridge.h"

#include <set>

namespace {

class NotificationPlatformBridgeQnx : public NotificationPlatformBridge {
 public:
  NotificationPlatformBridgeQnx() = default;
  ~NotificationPlatformBridgeQnx() override = default;

  void Display(NotificationHandler::Type notification_type,
               Profile* profile,
               const message_center::Notification& notification,
               std::unique_ptr<NotificationCommon::Metadata> metadata) override {}

  void Close(Profile* profile, const std::string& notification_id) override {}

  void GetDisplayed(Profile* profile,
                    GetDisplayedNotificationsCallback callback) const override {
    std::move(callback).Run(std::set<std::string>(),
                            /*supports_synchronization=*/false);
  }

  void GetDisplayedForOrigin(
      Profile* profile,
      const GURL& origin,
      GetDisplayedNotificationsCallback callback) const override {
    std::move(callback).Run(std::set<std::string>(),
                            /*supports_synchronization=*/false);
  }

  void SetReadyCallback(NotificationBridgeReadyCallback callback) override {
    std::move(callback).Run(true);
  }

  void DisplayServiceShutDown(Profile* profile) override {}
};

}  // namespace

bool NotificationPlatformBridge::CanHandleType(NotificationHandler::Type type) {
  return true;
}

std::unique_ptr<NotificationPlatformBridge> NotificationPlatformBridge::Create() {
  return std::make_unique<NotificationPlatformBridgeQnx>();
}
