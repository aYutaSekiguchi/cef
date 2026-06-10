// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPCZ_SRC_REFERENCE_DRIVERS_QNX_QNX_SHM_HANDLE_H_
#define IPCZ_SRC_REFERENCE_DRIVERS_QNX_QNX_SHM_HANDLE_H_

#include "build/build_config.h"

#if BUILDFLAG(IS_QNX)

#include "reference_drivers/file_descriptor.h"
#include "reference_drivers/object.h"

namespace ipcz::reference_drivers {

// QNX counterpart of Linux's FileDescriptor-backed Object used by
// WrappedFileDescriptor. QNX does not expose memfd_create() or fcntl seals,
// so the multiprocess reference driver carries shared-memory object
// identities through a QnxShmHandle that owns the underlying shm_open
// FileDescriptor and the mapped region.
//
// Thread-safety, lifetime, and ownership semantics match
// WrappedFileDescriptor on Linux.
class QnxShmHandle
    : public ObjectImpl<QnxShmHandle, Object::kQnxShmHandle> {
 public:
  explicit QnxShmHandle(FileDescriptor fd);
  QnxShmHandle(QnxShmHandle&&) = delete;
  QnxShmHandle& operator=(QnxShmHandle&&) = delete;
  ~QnxShmHandle() override;

  const FileDescriptor& file_descriptor() const { return fd_; }
  FileDescriptor TakeFileDescriptor() { return std::move(fd_); }

  static IpczDriverHandle Create(FileDescriptor fd) {
    return ReleaseAsHandle(MakeRefCounted<QnxShmHandle>(std::move(fd)));
  }

  static FileDescriptor UnwrapHandle(IpczDriverHandle handle) {
    return TakeFromHandle(handle)->TakeFileDescriptor();
  }

 private:
  FileDescriptor fd_;
};

}  // namespace ipcz::reference_drivers

#endif  // BUILDFLAG(IS_QNX)

#endif  // IPCZ_SRC_REFERENCE_DRIVERS_QNX_QNX_SHM_HANDLE_H_
