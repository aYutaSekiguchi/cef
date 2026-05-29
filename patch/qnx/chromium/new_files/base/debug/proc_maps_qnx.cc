// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license
// found in the LICENSE file.

#include "base/debug/proc_maps_linux.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/procfs.h>
#include <unistd.h>
#include <vector>

#include "base/check.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/strings/string_split.h"

namespace base {
namespace debug {

// MappedMemoryRegion implementations.
// These are defined in proc_maps_linux.cc on Linux but QNX uses this file.
MappedMemoryRegion::MappedMemoryRegion() = default;
MappedMemoryRegion::MappedMemoryRegion(const MappedMemoryRegion&) = default;
MappedMemoryRegion::MappedMemoryRegion(MappedMemoryRegion&&) noexcept = default;
MappedMemoryRegion& MappedMemoryRegion::operator=(MappedMemoryRegion&) =
    default;
MappedMemoryRegion& MappedMemoryRegion::operator=(
    MappedMemoryRegion&&) noexcept = default;

bool ReadProcMaps(std::string* proc_maps) {
  // QNX does not have /proc/self/maps.
  // Instead, we use devctl with DCMD_PROC_MAPINFO on /proc/self/as.
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/as", getpid());

  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;

  // First, get the number of mappings via DCMD_PROC_MAPINFO with NULL buffer.
  int raw_num_maps = 0;
  int status = devctl(fd, DCMD_PROC_MAPINFO, nullptr, 0, &raw_num_maps);
  if (status != EOK || raw_num_maps <= 0) {
    close(fd);
    return false;
  }

  size_t num_maps = static_cast<size_t>(raw_num_maps);
  procfs_mapinfo* maps = new procfs_mapinfo[num_maps];
  status = devctl(fd, DCMD_PROC_MAPINFO, maps,
                  num_maps * sizeof(procfs_mapinfo), &raw_num_maps);
  close(fd);

  if (status != EOK) {
    delete[] maps;
    return false;
  }

  // Convert to /proc/self/maps format string.
  std::string result;
  for (size_t i = 0; i < num_maps; ++i) {
    // Format: start-end perms offset dev:inode pathname
    // Simplified format for Chromium consumption.
    char line[256];
    snprintf(line, sizeof(line),
             "%" PRIx64 "-%" PRIx64 " %c%c%c%c 00000000 00:00 0 \n",
             maps[i].vaddr, maps[i].vaddr + maps[i].size,
             (maps[i].flags & PROT_READ) ? 'r' : '-',
             (maps[i].flags & PROT_WRITE) ? 'w' : '-',
             (maps[i].flags & PROT_EXEC) ? 'x' : '-',
             'p');  // Assume private
    result += line;
  }

  delete[] maps;
  *proc_maps = result;
  return true;
}

bool ParseProcMaps(std::string_view input,
                   std::vector<MappedMemoryRegion>* regions_out) {
  CHECK(regions_out);
  std::vector<MappedMemoryRegion> regions;

  std::vector<std::string> lines =
      SplitString(input, "\n", TRIM_WHITESPACE, SPLIT_WANT_ALL);

  for (size_t i = 0; i < lines.size(); ++i) {
    // Due to splitting on '\n' the last line should be empty.
    if (i == lines.size() - 1) {
      if (!lines[i].empty()) {
        DLOG(WARNING) << "Last line not empty";
        return false;
      }
      break;
    }

    MappedMemoryRegion region;
    const char* line = lines[i].c_str();
    char permissions[5] = {};
    uint8_t dev_major = 0;
    uint8_t dev_minor = 0;
    long inode = 0;
    int path_index = 0;

    // Sample format from man 5 proc:
    //
    // address           perms offset  dev   inode   pathname
    // 08048000-08056000 r-xp 00000000 03:0c 64593   /usr/sbin/gpm
    if (UNSAFE_TODO(sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4c %llx %hhx:%hhx %ld %n",
                           &region.start, &region.end, permissions,
                           &region.offset, &dev_major, &dev_minor, &inode,
                           &path_index)) < 7) {
      DPLOG(WARNING) << "sscanf failed for line: " << line;
      return false;
    }

    region.inode = inode;
    region.dev_major = dev_major;
    region.dev_minor = dev_minor;

    region.permissions = 0;

    if (permissions[0] == 'r') {
      region.permissions |= MappedMemoryRegion::READ;
    } else if (permissions[0] != '-') {
      return false;
    }

    if (permissions[1] == 'w') {
      region.permissions |= MappedMemoryRegion::WRITE;
    } else if (permissions[1] != '-') {
      return false;
    }

    if (permissions[2] == 'x') {
      region.permissions |= MappedMemoryRegion::EXECUTE;
    } else if (permissions[2] != '-') {
      return false;
    }

    if (permissions[3] == 'p') {
      region.permissions |= MappedMemoryRegion::PRIVATE;
    } else if (permissions[3] != 's' &&
               permissions[3] != 'S') {  // Shared memory.
      return false;
    }

    // Pushing then assigning saves us a string copy.
    regions.push_back(region);
    regions.back().path.assign(UNSAFE_TODO(line + path_index));
  }

  *regions_out = std::move(regions);
  return true;
}

}  // namespace debug
}  // namespace base
