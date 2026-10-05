#pragma once

#include "app/browser/browser_controller.h"
#include "include/cef_client.h"
#include "include/cef_values.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_frame.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_process_message.h"
#include "include/cef_request.h"
#include "include/cef_request_handler.h"
#include "include/wrapper/cef_message_router.h"

#include <memory>
#include <string>

namespace shelter {

class BridgeHandler;
class BrowserWindow;

class Client final : public CefClient,
                     public CefLifeSpanHandler,
                     public CefDisplayHandler,
                     public CefLoadHandler,
                     public CefRequestHandler,
                     public CefDownloadHandler {
 public:
  Client(BrowserWindow* window, BrowserController* controller,
         std::string tab_id, bool is_ui);
  ~Client() override;

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }

  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popup_features,
                     CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 CefRequestHandler::TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool is_loading,
                            bool can_go_back,
                            bool can_go_forward) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                              bool fullscreen) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

 private:
  BrowserWindow* window_ = nullptr;
  BrowserController* controller_ = nullptr;
  std::string tab_id_;
  bool is_ui_ = false;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  std::unique_ptr<BridgeHandler> bridge_handler_;

  IMPLEMENT_REFCOUNTING(Client);
};

}  // namespace shelter
