// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/icon_loader.h"

#include "base/functional/bind.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"

// QNX analogue of icon_loader_auralinux.cc. QNX has no xdg-mime integration
// and no LinuxUi desktop theme integration yet (ozone is not ported), so:
//   - GroupForFilepath() returns an empty IconGroup; downstream consumers
//     fall back to a generic icon.
//   - ReadIcon() returns an empty gfx::Image; consumers fall back to a
//     default icon.
//
// TODO(crbug.com/qnx-port): implement MIME detection via libmagic or a
//   small extension-based table once a QNX desktop integration exists,
//   and wire up ui::QnxUi for themed icons once ozone is ported.

// static
IconLoader::IconGroup IconLoader::GroupForFilepath(
    const base::FilePath& file_path) {
  // Returning an empty IconGroup signals "unknown" to IconLoadedCallback
  // consumers; they fall back to a generic icon when the group is empty.
  return IconGroup();
}

// static
scoped_refptr<base::TaskRunner> IconLoader::GetReadIconTaskRunner() {
  // Linux routes ReadIcon() through the UI thread because LinuxUi's icon
  // loader is GTK-bound. QNX has no GTK; reuse the UI thread so the
  // empty-image fallback stays on a deterministic thread, matching the
  // rest of the chromium UI stack.
  return content::GetUIThreadTaskRunner({});
}

void IconLoader::ReadIcon() {
  int size_pixels = 0;
  switch (icon_size_) {
    case IconLoader::SMALL: size_pixels = 16; break;
    case IconLoader::NORMAL: size_pixels = 32; break;
    case IconLoader::LARGE: size_pixels = 48; break;
    default: NOTREACHED();
  }

  // TODO(crbug.com/qnx-port): load themed icons via ui::QnxUi once ozone
  // is ported. For now, return an empty image so callers see
  // IsEmpty()=true and fall back to a default icon.
  gfx::Image image;

  target_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(callback_), std::move(image), group_));
  delete this;
}