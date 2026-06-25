// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/video_capture_gpu_channel_host.h"

namespace gpu {
class SharedImageInterface;
}  // namespace gpu

namespace media {

VideoCaptureGpuChannelHost::VideoCaptureGpuChannelHost() = default;
VideoCaptureGpuChannelHost::~VideoCaptureGpuChannelHost() = default;

VideoCaptureGpuChannelHost& VideoCaptureGpuChannelHost::GetInstance() {
  static base::NoDestructor<VideoCaptureGpuChannelHost> instance;
  return *instance;
}

scoped_refptr<gpu::SharedImageInterface>
VideoCaptureGpuChannelHost::GetSharedImageInterface() {
  return nullptr;
}

void VideoCaptureGpuChannelHost::OnContextLost() {}

void VideoCaptureGpuChannelHost::AddObserver(
    VideoCaptureGpuContextLostObserver* observer) {}

void VideoCaptureGpuChannelHost::RemoveObserver(
    VideoCaptureGpuContextLostObserver* to_remove) {}

}  // namespace media