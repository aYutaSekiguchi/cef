// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reference_drivers/qnx/qnx_shm_handle.h"

#if BUILDFLAG(IS_QNX)

#include "reference_drivers/file_descriptor.h"

namespace ipcz::reference_drivers {

QnxShmHandle::QnxShmHandle(FileDescriptor fd) : fd_(std::move(fd)) {}
QnxShmHandle::~QnxShmHandle() = default;

}  // namespace ipcz::reference_drivers

#endif  // BUILDFLAG(IS_QNX)
