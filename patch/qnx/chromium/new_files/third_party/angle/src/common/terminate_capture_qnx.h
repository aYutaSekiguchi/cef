// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMMON_TERMINATE_CAPTURE_QNX_H_
#define COMMON_TERMINATE_CAPTURE_QNX_H_

#if defined(__QNXNTO__)

namespace angle
{
namespace qnx_terminate
{

void Install();

}  // namespace qnx_terminate
}  // namespace angle

#endif  // defined(__QNXNTO__)

#endif  // COMMON_TERMINATE_CAPTURE_QNX_H_
