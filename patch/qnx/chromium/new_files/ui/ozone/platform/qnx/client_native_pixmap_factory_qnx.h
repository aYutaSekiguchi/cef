// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_CLIENT_NATIVE_PIXMAP_FACTORY_QNX_H_
#define UI_OZONE_PLATFORM_QNX_CLIENT_NATIVE_PIXMAP_FACTORY_QNX_H_

namespace gfx {
class ClientNativePixmapFactory;
}

namespace ui {

// Constructor hook for use in ui/ozone/platform/constructor_list.cc.
// Mirrors CreateClientNativePixmapFactoryHeadless from the headless platform.
gfx::ClientNativePixmapFactory* CreateClientNativePixmapFactoryQnx();

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_CLIENT_NATIVE_PIXMAP_FACTORY_QNX_H_
