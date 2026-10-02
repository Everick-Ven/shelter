#pragma once
#include <memory>
#include <string>
#include "include/cef_client.h"
#include "include/cef_download_handler.h"
#include "include/wrapper/cef_message_router.h"
namespace shelter {
class App;
class BridgeHandler;
// Per-browser client. Role decides which native events are forwarded into the
// UI page: the UI browser owns the message-router bridge, content browsers
// report title/loading/address changes so the UI can keep tabs in sync.
class Client final : public CefClient,
                     public CefLifeSpanHandler,
                     public CefDisplayHandler,
                     public CefLoadHandler,
                     public CefDownloadHandler {
 public:
  enum class Role { kUi, kContent };
  Client(App* app, Role role, std::string tab_id);
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>,
                                CefRefPtr<CefFrame>,
                                CefProcessId,
                                CefRefPtr<CefProcessMessage>) override;
  void OnAfterCreated(CefRefPtr<CefBrowser>) override;
  bool DoClose(CefRefPtr<CefBrowser>) override;
  void OnBeforeClose(CefRefPtr<CefBrowser>) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser>,
                     CefRefPtr<CefFrame>,
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
                     bool* no_javascript_access) override;
  void OnTitleChange(CefRefPtr<CefBrowser>, const CefString&) override;
  void OnAddressChange(CefRefPtr<CefBrowser>,
                       CefRefPtr<CefFrame>,
                       const CefString&) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser>,
                            bool,
                            bool,
                            bool) override;
  void OnLoadStart(CefRefPtr<CefBrowser>,
                   CefRefPtr<CefFrame>,
                   TransitionType) override;
  void OnLoadError(CefRefPtr<CefBrowser>,
                   CefRefPtr<CefFrame>,
                   ErrorCode,
                   const CefString&,
                   const CefString&) override;
  void OnLoadEnd(CefRefPtr<CefBrowser>,
                 CefRefPtr<CefFrame>,
                 int httpStatusCode) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser>,
                        CefRefPtr<CefDownloadItem>,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback>) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser>,
                         CefRefPtr<CefDownloadItem>,
                         CefRefPtr<CefDownloadItemCallback>) override;
 private:
  App* app_;
  Role role_;
  std::string tab_id_;
  // Declared before router_: destroyed after the router (Handler lifetime
  // requirement of CefMessageRouterBrowserSide::AddHandler).
  std::unique_ptr<BridgeHandler> bridge_handler_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(Client);
};
}  // namespace shelter
