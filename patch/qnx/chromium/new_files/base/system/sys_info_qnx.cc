// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// QNX SDP 8 implementation of SysInfo functions not covered by
// sys_info_posix.cc.  Uses POSIX sysconf() for memory queries (both
// _SC_PHYS_PAGES and _SC_AVPHYS_PAGES are confirmed available on QNX 8)
// and uname(2) for hardware identification.

#include "base/system/sys_info.h"

#include <stdint.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "base/byte_size.h"
#include "base/numerics/safe_conversions.h"

namespace base {

namespace {

// Helper: multiply two sysconf values, returning 0 on error.
ByteSize SysconfPages(int pages_name) {
  long pages = sysconf(pages_name);
  long page_size = sysconf(_SC_PAGESIZE);
  if (pages <= 0 || page_size <= 0) {
    return ByteSize(0);
  }
  return ByteSize(static_cast<uint64_t>(page_size)) *
         static_cast<uint64_t>(pages);
}

}  // namespace

// static
ByteSize SysInfo::AmountOfTotalPhysicalMemoryImpl() {
  // _SC_PHYS_PAGES: total physical pages in the system.
  // Confirmed available on QNX SDP 8 (confname.h line 260).
  return SysconfPages(_SC_PHYS_PAGES);
}

// static
ByteSize SysInfo::AmountOfAvailablePhysicalMemoryImpl() {
  // _SC_AVPHYS_PAGES: physical pages not currently in use.
  // Confirmed available on QNX SDP 8 (confname.h line 218).
  return SysconfPages(_SC_AVPHYS_PAGES);
}

// static
std::string SysInfo::CPUModelName() {
  // QNX does not expose /proc/cpuinfo.  Return the machine architecture
  // from uname(2) as a reasonable substitute (e.g. "x86_64", "aarch64").
  struct utsname info;
  if (uname(&info) == 0) {
    return std::string(info.machine);
  }
  return std::string();
}

// static
SysInfo::HardwareInfo SysInfo::GetHardwareInfoSync() {
  // QNX SDP 8 does not expose DMI/SMBIOS hardware info via a standard
  // interface.  Return empty strings; this data is used for telemetry only.
  return HardwareInfo{};
}

}  // namespace base
