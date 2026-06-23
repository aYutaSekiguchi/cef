#include "content/public/browser/weak_document_ptr.h"
// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/external_protocol/external_protocol_handler.h"

#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace external_protocol {

void RunExternalProtocolDialog(
    const GURL& url,
    content::WebContents* web_contents,
    ui::PageTransition page_transition,
    bool is_already_running,
    bool is_in_fenced_frame_tree,
    const std::optional<url::Origin>& initiator,
    content::WeakDocumentPtr initiator_document,
    const std::u16string& program_name) {}

}  // namespace external_protocol

// Stub for the class method
void ExternalProtocolHandler::RunExternalProtocolDialog(
    const GURL& url,
    content::WebContents* web_contents,
    ui::PageTransition page_transition,
    bool has_user_gesture,
    bool is_in_fenced_frame_tree,
    const std::optional<url::Origin>& initiating_origin,
    content::WeakDocumentPtr initiator_document,
    const std::u16string& program_name) {}
