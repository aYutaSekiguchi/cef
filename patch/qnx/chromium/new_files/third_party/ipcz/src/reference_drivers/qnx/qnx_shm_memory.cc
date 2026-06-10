// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reference_drivers/qnx/qnx_shm_memory.h"

#if BUILDFLAG(IS_QNX)

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "absl/base/macros.h"
#include "build/build_config.h"

namespace ipcz::reference_drivers {

QnxShmMemory::QnxShmMemory(FileDescriptor fd, void* address, size_t size)
    : fd_(std::move(fd)), address_(address), size_(size) {}

QnxShmMemory::~QnxShmMemory() {
  Close();
}

void QnxShmMemory::Close() {
  if (address_) {
    munmap(address_, size_);
    address_ = nullptr;
  }
  fd_.reset();
  size_ = 0;
}

// static
Ref<QnxShmMemory> QnxShmMemory::Create(size_t size) {
  // SHM_ANON is a QNX extension that gives a shm_open() handle without a
  // named object, mirroring the anonymous flavor of Linux memfd_create().
  // See <sys/mman.h> on QNX SDP 8.0.
  int raw_fd = shm_open(SHM_ANON, O_CREAT | O_RDWR, 0600);
  if (raw_fd < 0) {
    return nullptr;
  }
  FileDescriptor fd(raw_fd);

  if (ftruncate(raw_fd, static_cast<off_t>(size)) != 0) {
    return nullptr;
  }

  void* address =
      mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, raw_fd, 0);
  if (address == MAP_FAILED) {
    return nullptr;
  }

  return AdoptRef(new QnxShmMemory(std::move(fd), address, size));
}

}  // namespace ipcz::reference_drivers

#endif  // BUILDFLAG(IS_QNX)
