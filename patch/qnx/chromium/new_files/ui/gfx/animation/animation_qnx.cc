// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/animation/animation.h"

#include "ui/qnx/qnx_ui.h"

namespace gfx {

namespace {

// Mirrors the upstream animation_linux.cc algorithm but queries ui::QnxUi
// instead of ui::LinuxUi. QNX has no Linux-style toolkit configuration to
// ask, so the stub QnxUi currently always reports animations as enabled.
// The null check is defensive: in the current QNX build QnxUi::instance()
// never returns nullptr, but future ozone integration might.
bool AnimationsEnabled() {
  auto* qnx_ui = ui::QnxUi::instance();
  return !qnx_ui || qnx_ui->AnimationsEnabled();
}

}  // namespace

// static
bool Animation::ShouldRenderRichAnimationImpl() {
  return AnimationsEnabled();
}

// static
bool Animation::ScrollAnimationsEnabledBySystem() {
  return AnimationsEnabled();
}

// static
void Animation::UpdatePrefersReducedMotion() {
  prefers_reduced_motion_ = !AnimationsEnabled();
}

}  // namespace gfx