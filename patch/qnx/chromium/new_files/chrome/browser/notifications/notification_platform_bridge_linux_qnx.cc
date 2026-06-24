// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/notification_platform_bridge.h"

bool NotificationPlatformBridge::CanHandleType(
    NotificationHandler::Type type) {
  return false;
}

std::unique_ptr<NotificationPlatformBridge> NotificationPlatformBridge::Create() {
  return nullptr;
}
