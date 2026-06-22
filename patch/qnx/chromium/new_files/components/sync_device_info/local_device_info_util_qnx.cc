// Copyright 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <limits.h>
#include <unistd.h>

#include <string>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

namespace syncer {

std::string GetPersonalizableDeviceNameInternal() {
  char hostname[HOST_NAME_MAX];
  if (gethostname(hostname, HOST_NAME_MAX) == 0) {
    return hostname;
  }
  return "QNX";
}

}  // namespace syncer
