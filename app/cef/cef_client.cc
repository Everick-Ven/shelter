#include "app/cef/cef_client.h"

#include "app/browser/browser_window.h"
#include "app/common/logging.h"
#include "app/common/url_utils.h"
#include "include/cef_parser.h"
#include "include/cef_value.h"

#include <utility>

namespace shelter {
namespace {

class BridgeHandler final : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit BridgeHandler(BrowserWindow* window) : window_(window) {}

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    CefRefPtr<CefValue> value = CefParseJSON(request, JSON_PARSER_RFC);
    if (!value || value->GetType() != VTYPE_DICTIONARY) {
      callback->Failure(400, "Invalid SHELTER bridge request");
      return true;
    }
    auto arguments = value->GetDictionary();
    if (!arguments || arguments->GetString("ns").ToString() != "shelter") return false;

    const std::string command = arguments->GetString("cmd").ToString();
    if (command.empty() || !window_) {
      callback->Failure(400, "Missing SHELTER bridge command");
      return true;
    }

    std::string response;
    if (!window_->HandleBridgeCommand(browser, frame, command, arguments, response)) {
      callback->Failure(403, "SHELTER bridge request rejected");
      return true;
    }
    callback->Success(response);
    return true;
  }

  void OnQueryCanceled(CefRefPtr<CefBrowser>,
                       CefRefPtr<CefFrame>,
                       int64_t) override {}

 private:
  BrowserWindow* window_ = nullptr;

  IMPLEMENT_REFCOUNTING(BridgeHandler);
};

}  // namespace

Client::Client(BrowserWindow* window, BrowserController* controller,
               std::string tab_id, bool is_ui)
    : window_(window),
      controller_(controller),
      tab_id_(std::move(tab_id)),
      is_ui_(is_ui) {
  if (is_ui_) {
    router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig());
    if (router_) router_->AddHandler(new BridgeHandler(window_), false);
  }
}

bool Client::OnBeforePopup(CefRefPtr<CefBrowser>,
                           CefRefPtr<CefFrame>,
                           const CefString& target_url,
                           const CefString&,
                           WindowOpenDisposition,
                           bool,
                           const CefPopupFeatures&,
                           CefWindowInfo&,
                           CefRefPtr<CefClient>&,
                           CefBrowserSettings&,
                           CefRefPtr<CefDictionaryValue>&,
                           bool*) {
  if (!is_ui_ && window_ && IsValidNavigationUrl(target_url.ToString())) {
    window_->OpenPopupInNewTab(target_url.ToString());
  }
  // SHELTER owns one top-level window. Popups are either routed to a new tab
  // or blocked instead of creating an unmanaged Chromium window.
  return true;
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  if (router_) router_->OnAfterCreated(browser);
  if (is_ui_) {
    if (window_) window_->OnUiBrowserCreated(browser);
    return;
  }
  if (controller_) controller_->Attach(tab_id_, browser);
  if (window_) window_->OnWebBrowserCreated(tab_id_, browser);
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  if (router_) router_->OnBeforeClose(browser);
  if (is_ui_) {
    if (window_) window_->OnUiBrowserClosed(browser);
  } else if (window_) {
    window_->OnWebBrowserClosed(tab_id_, browser);
  }
}

bool Client::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest>,
                            bool,
                            bool) {
  if (router_) router_->OnBeforeBrowse(browser, frame);
  return false;
}

void Client::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                       TerminationStatus status) {
  if (router_) router_->OnRenderProcessTerminated(browser);
  Log(LogLevel::Warning,
      "CEF renderer terminated: " + std::to_string(static_cast<int>(status)));
}

bool Client::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefProcessId source_process,
                                      CefRefPtr<CefProcessMessage> message) {
  return router_ &&
         router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

void Client::OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) {
  const std::string value = title.ToString();
  if (!is_ui_) {
    if (controller_) controller_->SetTitle(tab_id_, value);
    if (window_) window_->OnWebTitleChanged(tab_id_, value);
  }
  Log(LogLevel::Info, std::string("title: ") + value);
}

void Client::OnAddressChange(CefRefPtr<CefBrowser>,
                             CefRefPtr<CefFrame> frame,
                             const CefString& url) {
  if (is_ui_ || !frame || !frame->IsMain()) return;
  const std::string value = url.ToString();
  if (controller_) controller_->SetAddress(tab_id_, value);
  if (window_) window_->OnWebAddressChanged(tab_id_, value);
}

void Client::OnLoadingStateChange(CefRefPtr<CefBrowser>,
                                  bool is_loading,
                                  bool can_go_back,
                                  bool can_go_forward) {
  if (is_ui_) return;
  if (controller_) {
    controller_->SetLoading(tab_id_, is_loading);
    controller_->SetHistoryState(tab_id_, can_go_back, can_go_forward);
  }
  if (window_) {
    window_->OnWebLoadingChanged(tab_id_, is_loading, can_go_back,
                                 can_go_forward);
  }
}

void Client::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                                    bool fullscreen) {
  if (!is_ui_ && window_) window_->OnWebFullscreenChanged(browser, fullscreen);
}

void Client::OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                       int) {
  if (is_ui_ && frame && frame->IsMain() && window_) window_->OnUiLoadEnd();
}

void Client::OnLoadError(CefRefPtr<CefBrowser>,
                         CefRefPtr<CefFrame>,
                         ErrorCode error_code,
                         const CefString&,
                         const CefString& failed_url) {
  Log(LogLevel::Warning,
      "navigation error: " + std::to_string(static_cast<int>(error_code)) +
          " url=" + failed_url.ToString());
}

bool Client::OnBeforeDownload(CefRefPtr<CefBrowser>,
                              CefRefPtr<CefDownloadItem> download_item,
                              const CefString&,
                              CefRefPtr<CefBeforeDownloadCallback> callback) {
  if (window_) window_->OnWebBeforeDownload(download_item, callback);
  else if (callback) callback->Cancel();
  return true;
}

void Client::OnDownloadUpdated(CefRefPtr<CefBrowser>,
                               CefRefPtr<CefDownloadItem> download_item,
                               CefRefPtr<CefDownloadItemCallback>) {
  if (window_) window_->OnWebDownloadUpdated(download_item);
}

}  // namespace shelter
