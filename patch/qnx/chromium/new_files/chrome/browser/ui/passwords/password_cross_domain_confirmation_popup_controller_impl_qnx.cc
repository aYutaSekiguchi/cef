// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/passwords/password_cross_domain_confirmation_popup_controller_impl.h"

#include "base/i18n/rtl.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/rect_f.h"
#include "url/gurl.h"

PasswordCrossDomainConfirmationPopupControllerImpl::
    PasswordCrossDomainConfirmationPopupControllerImpl(
        content::WebContents* web_contents) {}
PasswordCrossDomainConfirmationPopupControllerImpl::
    ~PasswordCrossDomainConfirmationPopupControllerImpl() = default;
void PasswordCrossDomainConfirmationPopupControllerImpl::Show(
    const gfx::RectF& element_bounds,
    base::i18n::TextDirection text_direction,
    const GURL& origin,
    const std::u16string& password,
    base::OnceClosure confirmation_callback,
    bool warning_deferred) {}
