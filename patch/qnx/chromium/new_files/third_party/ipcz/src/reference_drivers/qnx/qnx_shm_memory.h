// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_REFERENCE_DRIVERS_QNX_QNX_SHM_MEMORY_H_
#define IPCZ_SRC_REFERENCE_DRIVERS_QNX_QNX_SHM_MEMORY_H_

#include "build/build_config.h"

#if BUILDFLAG(IS_QNX)

#include <cstddef>
#include <cstdint>

#include "reference_drivers/file_descriptor.h"
#include "util/ref_counted.h"

namespace ipcz::reference_drivers {

// QNX implementation of a single-driver-object shm-backed memory region.
//
// QNX does not expose memfd_create() or fcntl seal syscalls; instead, the
// driver allocates an anonymous POSIX shm object via shm_open(SHM_ANON),
// sizes it with ftruncate(), mmaps MAP_SHARED, and exposes the resulting
// region together with the shm fd. Cross-process transfer is left to
// the multiprocess reference driver, which today only supports the Linux
// memfd flavor; future work can layer shm_open_handle() on top of this
// driver.
//
// Mirror API to MemfdMemory on Linux so that the reference driver pair
// can call the same Create/Close surface.
class QnxShmMemory {
 public:
  // Creates a QNX shm-backed memory region of `size` bytes. Returns an
  // empty Ref on failure. The returned object owns the mapped region and
  // the shm fd; both are released on Close.
  static Ref<QnxShmMemory> Create(size_t size);

  QnxShmMemory() = default;
  virtual ~QnxShmMemory();

  void* address() const { return address_; }
  size_t size() const { return size_; }
  const FileDescriptor& file_descriptor() const { return fd_; }

  // Closes the mapping and the shm object. Safe to call multiple times.
  void Close();

 protected:
  // Allows multiprocess driver to construct from a pre-imported shm fd.
  friend class MultiprocessReferenceDriver;
  QnxShmMemory(FileDescriptor fd, void* address, size_t size);

 private:
  FileDescriptor fd_;
  void* address_ = nullptr;
  size_t size_ = 0;
};

}  // namespace ipcz::reference_drivers

#endif  // BUILDFLAG(IS_QNX)

#endif  // IPCZ_SRC_REFERENCE_DRIVERS_QNX_QNX_SHM_MEMORY_H_
