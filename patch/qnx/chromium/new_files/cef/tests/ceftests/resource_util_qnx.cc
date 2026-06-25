// Copyright (c) 2026 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "tests/shared/browser/resource_util.h"

#include <stdio.h>
#include <string.h>

namespace client {

bool GetResourceDir(std::string& dir) {
  char buff[1024];

  // QNX exposes the current executable as /proc/self/exefile. Unlike Linux's
  // /proc/self/exe this is not a symlink, so read the path from procfs.
  FILE* fp = fopen("/proc/self/exefile", "r");
  if (!fp) {
    return false;
  }

  if (!fgets(buff, sizeof(buff), fp)) {
    fclose(fp);
    return false;
  }
  fclose(fp);

  // Strip any trailing newline that procfs may include.
  size_t len = strlen(buff);
  while (len > 0 && (buff[len - 1] == '\n' || buff[len - 1] == '\r')) {
    buff[--len] = 0;
  }

  // Remove the executable name from the path.
  char* pos = strrchr(buff, '/');
  if (!pos) {
    return false;
  }

  // Add "ceftests_files" to the path.
  strcpy(pos + 1, "ceftests_files");
  dir = std::string(buff);
  return true;
}

}  // namespace client
