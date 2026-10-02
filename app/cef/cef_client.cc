#include "app/cef/cef_client.h"

#include <filesystem>
#include <string>
#include <utility>

#include "app/cef/bridge.h"
#include "app/cef/cef_app.h"
#include "app/common/logging.h"
#include "app/common/url_utils.h"
#include "app/platform/platform.h"
#include "include/wrapper/cef_helpers.h"

namespace shelter {
namespace {
std::string SanitizeFileName(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c < 0x20) {
      out.push_back('_');
    } else {
      out.push_back(c);
    }
  }
  if (out.empty() || out == "." || out == "..") out = "download";
  return out;
}
}  // namespace

Client::Client(App* app, Role role, std::string tab_id)
    : app_(app), role_(role), tab_id_(std::move(tab_id)) {
  // Browser-side message router. Handlers are registered only for the UI
  // browser: queries from content pages get an automatic "no handler" reply.
  router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig());
  if (role_ == Role::kUi && app_) {
    bridge_handler_ = std::make_unique<BridgeHandler>(app_);
    router_->AddHandler(bridge_handler_.get(), /*first=*/false);
  }
}

bool Client::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefProcessId source_process,
                                      CefRefPtr<CefProcessMessage> message) {
  return router_ && router_->OnProcessMessageReceived(browser, frame,
                                                      source_process, message);
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!app_) return;
  if (role_ == Role::kUi) {
    app_->OnUiBrowserCreated(browser);
  } else {
    app_->controller().Attach(tab_id_, browser);
    app_->OnContentBrowserCreated(tab_id_, browser);
  }
}

bool Client::DoClose(CefRefPtr<CefBrowser> browser) {
  // Allow the close. For windowed browsers this results in the OS close event
  // being sent (App::RequestWindowClose orchestrates the multi-browser order).
  return false;
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (router_) router_->OnBeforeClose(browser);
  if (!app_) return;
  if (role_ == Role::kUi) {
    app_->OnUiBrowserClosed();
  } else {
    app_->OnContentBrowserClosed(tab_id_);
  }
}

bool Client::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           int popup_id,
                           const CefString& target_url,
                           const CefString& target_frame_name,
                           WindowOpenDisposition target_disposition,
                           bool user_gesture,
                           const CefPopupFeatures& popupFeatures,
                           CefWindowInfo& windowInfo,
                           CefRefPtr<CefClient>& client,
                           CefBrowserSettings& settings,
                           CefRefPtr<CefDictionaryValue>& extra_info,
                           bool* no_javascript_access) {
  // The UI never opens popups; content popups (target=_blank / window.open)
  // are redirected to the same tab so the UI stack stays authoritative.
  if (role_ == Role::kContent && browser) {
    const std::string url = target_url.ToString();
    if (IsValidNavigationUrl(url)) {
      browser->GetMainFrame()->LoadURL(url);
    }
  }
  return true;  // cancel the popup window
}

void Client::OnTitleChange(CefRefPtr<CefBrowser> browser,
                           const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ != Role::kContent || !app_) return;
  const std::string value = title.ToString();
  app_->controller().SetTitle(tab_id_, value);
  app_->OnContentTitle(tab_id_, value);
}

void Client::OnAddressChange(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame> frame,
                             const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ != Role::kContent || !app_ || !frame || !frame->IsMain()) return;
  const std::string value = url.ToString();
  app_->controller().SetAddress(tab_id_, value);
  app_->OnContentAddress(tab_id_, value);
}

void Client::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                  bool isLoading,
                                  bool canGoBack,
                                  bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ != Role::kContent || !app_) return;
  app_->controller().SetLoading(tab_id_, isLoading);
  app_->controller().SetHistoryState(tab_id_, canGoBack, canGoForward);
  app_->OnContentLoading(tab_id_, isLoading, canGoBack, canGoForward);
}

void Client::OnLoadStart(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         TransitionType transition_type) {
  // Renderer-side proof of life for the smoke test: a live renderer must
  // report navigation start. Logged for every role before role gating.
  if (frame && frame->IsMain()) {
    Log(LogLevel::Info, "load start " + frame->GetURL().ToString());
  }
  if (role_ != Role::kContent || !app_ || !frame || !frame->IsMain()) return;
  app_->OnContentLoadStart(tab_id_, transition_type);
}

void Client::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int httpStatusCode) {
  // Renderer-side proof of life: only a live renderer process reports load
  // completion. Used as the CI smoke-test marker.
  if (!frame || !frame->IsMain()) return;
  Log(LogLevel::Info, "load end " + frame->GetURL().ToString());
}

void Client::OnLoadError(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         ErrorCode errorCode,
                         const CefString& errorText,
                         const CefString& failedUrl) {
  if (errorCode == ERR_ABORTED) return;  // canceled navigation, not an error
  Log(LogLevel::Warning,
      std::string("navigation error: ") + std::to_string(errorCode) + " " +
          failedUrl.ToString());
}

bool Client::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefDownloadItem> download_item,
                              const CefString& suggested_name,
                              CefRefPtr<CefBeforeDownloadCallback> callback) {
  // Alloy style cancels downloads by default — always continue into the
  // user's Downloads folder without a save dialog.
  std::error_code ec;
  const std::filesystem::path dir = PlatformDefaultDownloadsDir();
  std::filesystem::create_directories(dir, ec);
  const std::filesystem::path target =
      dir / SanitizeFileName(suggested_name.ToString());
  callback->Continue(target.string(), /*show_dialog=*/false);
  return true;
}

void Client::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefDownloadItem> download_item,
                               CefRefPtr<CefDownloadItemCallback> callback) {}

}  // namespace shelter
