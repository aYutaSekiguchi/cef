// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// QNX SDP 8 implementation of SysInfo functions not covered by
// sys_info_posix.cc. Uses POSIX sysconf() for memory queries when available,
// syspage asinfo as a fallback for total physical memory, and uname(2) for
// hardware identification.

#include "base/system/sys_info.h"

#include <stdint.h>
extern "C" {
#include <sys/asinfo.h>
}
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

uint64_t AsinfoRangeSize(const asinfo_entry* entry) {
  if (!entry || entry->end < entry->start) {
    return 0;
  }
  return static_cast<uint64_t>(entry->end - entry->start + 1);
}

int SumAsinfoRange(asinfo_entry* entry, char* name, void* data) {
  auto* total = static_cast<uint64_t*>(data);
  *total += AsinfoRangeSize(entry);
  // QNX walk_asinfo() continues while the callback returns non-zero.
  return 1;
}

ByteSize AmountOfSystemRamFromAsinfo() {
  uint64_t total = 0;
  walk_asinfo("sysram", SumAsinfoRange, &total);
  return ByteSize(total);
}

}  // namespace

// static
ByteSize SysInfo::AmountOfTotalPhysicalMemoryImpl() {
  // _SC_PHYS_PAGES: total physical pages in the system. QNX 8 exposes the
  // constant, but it can return -1 (with errno left at 0) on QEMU. Fall back to
  // syspage asinfo's sysram ranges so callers never divide by zero when total
  // memory telemetry is sampled.
  ByteSize sysconf_bytes = SysconfPages(_SC_PHYS_PAGES);
  if (!sysconf_bytes.is_zero()) {
    return sysconf_bytes;
  }
  return AmountOfSystemRamFromAsinfo();
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
