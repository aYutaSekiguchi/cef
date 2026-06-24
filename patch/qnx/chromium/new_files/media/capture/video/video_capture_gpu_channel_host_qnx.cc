// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/video_capture_gpu_channel_host.h"

#include "gpu/ipc/client/client_shared_image_interface.h"

namespace media {

VideoCaptureGpuChannelHost::VideoCaptureGpuChannelHost() = default;
VideoCaptureGpuChannelHost::~VideoCaptureGpuChannelHost() = default;

VideoCaptureGpuChannelHost& VideoCaptureGpuChannelHost::GetInstance() {
  static VideoCaptureGpuChannelHost instance;
  return instance;
}

scoped_refptr<gpu::SharedImageInterface>
VideoCaptureGpuChannelHost::GetSharedImageInterface() {
  return nullptr;
}

}  // namespace media
