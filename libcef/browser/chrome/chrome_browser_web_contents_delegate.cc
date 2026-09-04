// Copyright 2026 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/chrome/chrome_browser_web_contents_delegate.h"

#include <utility>

#include "base/check.h"
#include "cef/libcef/browser/chrome/browser_delegate.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/find_in_page/find_tab_helper.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/keyboard_event_processing_result.h"

namespace cef {

// static
std::unique_ptr<BrowserWebContentsDelegate>
BrowserDelegate::CreateWebContentsDelegate(
    BrowserWindowInterface* browser,
    ExclusiveAccessManager& exclusive_access_manager,
    chrome::BrowserCommandController& command_controller,
    UnloadController& unload_controller,
    web_app::AppBrowserController* app_browser_controller,
    BrowserWindow& window,
    DesktopBrowserWindowCapabilities& capabilities,
    BrowserUiController& browser_ui_controller) {
  return std::make_unique<ChromeBrowserWebContentsDelegate>(
      browser, exclusive_access_manager, command_controller, unload_controller,
      app_browser_controller, window, capabilities, browser_ui_controller);
}

}  // namespace cef

ChromeBrowserWebContentsDelegate::ChromeBrowserWebContentsDelegate(
    BrowserWindowInterface* browser,
    ExclusiveAccessManager& exclusive_access_manager,
    chrome::BrowserCommandController& command_controller,
    UnloadController& unload_controller,
    web_app::AppBrowserController* app_browser_controller,
    BrowserWindow& window,
    DesktopBrowserWindowCapabilities& capabilities,
    BrowserUiController& browser_ui_controller)
    : BrowserWebContentsDelegate(browser,
                                 exclusive_access_manager,
                                 command_controller,
                                 unload_controller,
                                 app_browser_controller,
                                 window,
                                 capabilities,
                                 browser_ui_controller),
      browser_(*browser),
      has_app_browser_controller_(app_browser_controller != nullptr) {}

ChromeBrowserWebContentsDelegate::~ChromeBrowserWebContentsDelegate() = default;

content::KeyboardEventProcessingResult
ChromeBrowserWebContentsDelegate::PreHandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (browser_->cef_delegate()) {
    auto result =
        browser_->cef_delegate()->PreHandleKeyboardEvent(source, event);
    if (result != content::KeyboardEventProcessingResult::NOT_HANDLED) {
      return result;
    }
  }
  return BrowserWebContentsDelegate::PreHandleKeyboardEvent(source, event);
}

bool ChromeBrowserWebContentsDelegate::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (browser_->cef_delegate() &&
      browser_->cef_delegate()->HandleKeyboardEvent(source, event)) {
    return true;
  }
  return BrowserWebContentsDelegate::HandleKeyboardEvent(source, event);
}

content::PreloadingEligibility
ChromeBrowserWebContentsDelegate::IsPrerender2Supported(
    content::WebContents& web_contents,
    content::PreloadingTriggerType trigger_type) {
  // Prerender is not supported in CEF, including Chrome-created windows without
  // a CEF client. See issue #3664.
  return content::PreloadingEligibility::kPreloadingDisabled;
}

content::WebContents* ChromeBrowserWebContentsDelegate::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
#if DCHECK_IS_ON()
  DCHECK(params.Valid());
#endif

  // Chrome handles these cases before the CEF navigation callback. Keep these
  // conditions in sync with the early returns in the base implementation, and
  // let Chrome perform the navigation and manage the callback in those cases.
  if (browser_->GetType() == BrowserWindowInterface::Type::TYPE_DEVTOOLS) {
    return BrowserWebContentsDelegate::OpenURLFromTab(
        source, params, std::move(navigation_handle_callback));
  }
  if (params.disposition == WindowOpenDisposition::NEW_SPLIT_VIEW && source) {
    tabs::TabInterface* const source_tab =
        tabs::TabInterface::MaybeGetFromContents(source);
    if (source_tab && source_tab->IsSplit()) {
      const split_tabs::SplitTabId split_id = source_tab->GetSplit().value();
      for (tabs::TabInterface* tab :
           browser_->GetTabStripModel()->GetSplitData(split_id)->ListTabs()) {
        if (tab != source_tab) {
          return BrowserWebContentsDelegate::OpenURLFromTab(
              source, params, std::move(navigation_handle_callback));
        }
      }
    }
  }

  if (browser_->cef_delegate() &&
      !browser_->cef_delegate()->OpenURLFromTabEx(source, params,
                                                  navigation_handle_callback)) {
    return nullptr;
  }
  return BrowserWebContentsDelegate::OpenURLFromTab(
      source, params, std::move(navigation_handle_callback));
}

void ChromeBrowserWebContentsDelegate::LoadingStateChanged(
    content::WebContents* source,
    bool should_show_loading_ui) {
  BrowserWebContentsDelegate::LoadingStateChanged(source,
                                                  should_show_loading_ui);
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->LoadingStateChanged(source,
                                                  should_show_loading_ui);
  }
}

void ChromeBrowserWebContentsDelegate::SetContentsBounds(
    content::WebContents* source,
    const gfx::Rect& bounds) {
  if (browser_->cef_delegate() &&
      browser_->cef_delegate()->SetContentsBoundsEx(source, bounds)) {
    return;
  }
  BrowserWebContentsDelegate::SetContentsBounds(source, bounds);
}

void ChromeBrowserWebContentsDelegate::UpdateTargetURL(
    content::WebContents* source,
    const GURL& url) {
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->UpdateTargetURL(source, url);
  }
  BrowserWebContentsDelegate::UpdateTargetURL(source, url);
}

bool ChromeBrowserWebContentsDelegate::TakeFocus(content::WebContents* source,
                                                 bool reverse) {
  if (browser_->cef_delegate()) {
    return browser_->cef_delegate()->TakeFocus(source, reverse);
  }
  return BrowserWebContentsDelegate::TakeFocus(source, reverse);
}

bool ChromeBrowserWebContentsDelegate::DidAddMessageToConsole(
    content::WebContents* source,
    blink::mojom::ConsoleMessageLevel log_level,
    const std::u16string& message,
    int32_t line_no,
    const std::u16string& source_id) {
  if (browser_->cef_delegate()) {
    return browser_->cef_delegate()->DidAddMessageToConsole(
        source, log_level, message, line_no, source_id);
  }
  return BrowserWebContentsDelegate::DidAddMessageToConsole(
      source, log_level, message, line_no, source_id);
}

void ChromeBrowserWebContentsDelegate::DraggableRegionsChanged(
    const std::vector<blink::mojom::DraggableRegionPtr>& regions,
    content::WebContents* contents) {
  if (has_app_browser_controller_) {
    BrowserWebContentsDelegate::DraggableRegionsChanged(regions, contents);
  } else if (browser_->cef_delegate()) {
    browser_->cef_delegate()->DraggableRegionsChanged(regions, contents);
  }
}

void ChromeBrowserWebContentsDelegate::WebContentsCreated(
    content::WebContents* source_contents,
    const content::GlobalRenderFrameHostId& opener_id,
    const std::string& frame_name,
    const GURL& target_url,
    content::WebContents* new_contents) {
  // SessionTabHelper must exist before CEF initializes the popup browser host.
  BrowserWebContentsDelegate::WebContentsCreated(
      source_contents, opener_id, frame_name, target_url, new_contents);
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->WebContentsCreated(
        source_contents, opener_id, frame_name, target_url, new_contents);
  }
}

void ChromeBrowserWebContentsDelegate::RendererUnresponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host,
    base::RepeatingClosure hang_monitor_restarter) {
  if (browser_->cef_delegate() &&
      browser_->cef_delegate()->RendererUnresponsiveEx(
          source, render_widget_host, hang_monitor_restarter)) {
    return;
  }
  BrowserWebContentsDelegate::RendererUnresponsive(
      source, render_widget_host, std::move(hang_monitor_restarter));
}

void ChromeBrowserWebContentsDelegate::RendererResponsive(
    content::WebContents* source,
    content::RenderWidgetHost* render_widget_host) {
  if (browser_->cef_delegate() &&
      browser_->cef_delegate()->RendererResponsiveEx(source,
                                                     render_widget_host)) {
    return;
  }
  BrowserWebContentsDelegate::RendererResponsive(source, render_widget_host);
}

content::JavaScriptDialogManager*
ChromeBrowserWebContentsDelegate::GetJavaScriptDialogManager(
    content::WebContents* source) {
  if (browser_->cef_delegate()) {
    auto* cef_js_dialog_manager =
        browser_->cef_delegate()->GetJavaScriptDialogManager(source);
    if (cef_js_dialog_manager) {
      return cef_js_dialog_manager;
    }
  }
  return BrowserWebContentsDelegate::GetJavaScriptDialogManager(source);
}

void ChromeBrowserWebContentsDelegate::EnterFullscreenModeForTab(
    content::RenderFrameHost* requesting_frame,
    const blink::mojom::FullscreenOptions& options) {
  BrowserWebContentsDelegate::EnterFullscreenModeForTab(requesting_frame,
                                                        options);
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->EnterFullscreenModeForTab(requesting_frame,
                                                        options);
  }
}

void ChromeBrowserWebContentsDelegate::ExitFullscreenModeForTab(
    content::WebContents* web_contents) {
  BrowserWebContentsDelegate::ExitFullscreenModeForTab(web_contents);
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->ExitFullscreenModeForTab(web_contents);
  }
}

void ChromeBrowserWebContentsDelegate::FindReply(
    content::WebContents* web_contents,
    int request_id,
    int number_of_matches,
    const gfx::Rect& selection_rect,
    int active_match_ordinal,
    bool final_update) {
  // Match Chrome's early return: CEF is notified only for contents with a find
  // helper, and only after that helper has processed the reply.
  if (!find_in_page::FindTabHelper::FromWebContents(web_contents)) {
    return;
  }
  BrowserWebContentsDelegate::FindReply(web_contents, request_id,
                                        number_of_matches, selection_rect,
                                        active_match_ordinal, final_update);
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->FindReply(web_contents, request_id,
                                        number_of_matches, selection_rect,
                                        active_match_ordinal, final_update);
  }
}

void ChromeBrowserWebContentsDelegate::UpdatePreferredSize(
    content::WebContents* source,
    const gfx::Size& pref_size) {
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->UpdatePreferredSize(source, pref_size);
  }
}

void ChromeBrowserWebContentsDelegate::ResizeDueToAutoResize(
    content::WebContents* source,
    const gfx::Size& new_size) {
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->ResizeDueToAutoResize(source, new_size);
  }
}

void ChromeBrowserWebContentsDelegate::CanDownload(
    const GURL& url,
    const std::string& request_method,
    base::OnceCallback<void(bool)> callback) {
  if (browser_->cef_delegate()) {
    browser_->cef_delegate()->CanDownload(url, request_method,
                                          std::move(callback));
    return;
  }
  BrowserWebContentsDelegate::CanDownload(url, request_method,
                                          std::move(callback));
}

void ChromeBrowserWebContentsDelegate::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  if (browser_->cef_delegate()) {
    callback = browser_->cef_delegate()->RequestMediaAccessPermissionEx(
        web_contents, request, std::move(callback));
    if (callback.is_null()) {
      return;
    }
  }
  BrowserWebContentsDelegate::RequestMediaAccessPermission(
      web_contents, request, std::move(callback));
}
