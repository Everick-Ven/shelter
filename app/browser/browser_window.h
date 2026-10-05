#pragma once

#include "app/browser/browser_controller.h"
#include "include/cef_values.h"
#include "include/cef_download_handler.h"
#include "include/cef_frame.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_window.h"

#include <map>
#include <string>

namespace shelter {

class BrowserWindow final : public CefBaseRefCounted {
 public:
  BrowserWindow();

  void Create();
  bool HandleBridgeCommand(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           const std::string& command,
                           CefRefPtr<CefDictionaryValue> arguments,
                           std::string& response);

  void OnUiBrowserCreated(CefRefPtr<CefBrowser> browser);
  void OnUiLoadEnd();
  void OnUiBrowserClosed(CefRefPtr<CefBrowser> browser);
  void OnWebBrowserCreated(const std::string& tab_id, CefRefPtr<CefBrowser> browser);
  void OnWebBrowserClosed(const std::string& tab_id, CefRefPtr<CefBrowser> browser);
  void OnWebAddressChanged(const std::string& tab_id, const std::string& url);
  void OnWebTitleChanged(const std::string& tab_id, const std::string& title);
  void OnWebLoadingChanged(const std::string& tab_id,
                           bool loading,
                           bool can_go_back,
                           bool can_go_forward);
  void OnWebFullscreenChanged(CefRefPtr<CefBrowser> browser, bool fullscreen);
  void OnWebBeforeDownload(CefRefPtr<CefDownloadItem> item,
                           CefRefPtr<CefBeforeDownloadCallback> callback);
  void OnWebDownloadUpdated(CefRefPtr<CefDownloadItem> item);
  void OpenPopupInNewTab(const std::string& url);
  void CloseWindow();
  void OnWindowClosing(CefRefPtr<CefWindow> window);
  void OnWindowDestroyed(CefRefPtr<CefWindow> window);

 private:
  struct WebTab {
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefOverlayController> overlay;
    CefRefPtr<CefBrowser> browser;
    std::string requested_url;
    std::string pending_url;
    bool closing = false;
  };

  struct PendingDownload {
    CefRefPtr<CefBeforeDownloadCallback> callback;
    std::string filename;
  };

  bool EnsureWebTab(const std::string& tab_id, const std::string& url);
  void NavigateWebTab(std::string tab_id, std::string url);
  void SetWebContentVisible(bool visible);
  void UpdateWebTabBoundsAndVisibility();
  void CloseWebTab(const std::string& tab_id);
  void DispatchToUi(const std::string& event,
                    CefRefPtr<CefDictionaryValue> data = nullptr);
  void DispatchWebTabState(const std::string& tab_id);
  bool IsTrustedUiFrame(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) const;
  void ContinueDownload(int download_id, const std::string& action);
  void MaybeQuitMessageLoop();
  std::string GetDownloadsDirectory() const;
  std::string GetUniqueDownloadPath(const std::string& filename, int download_id) const;

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> ui_view_;
  CefRefPtr<CefBrowser> ui_browser_;
  BrowserController controller_;
  std::map<std::string, WebTab> web_tabs_;
  std::map<int, PendingDownload> pending_downloads_;
  CefRect viewport_bounds_;
  std::string active_tab_id_;
  bool content_visible_ = false;
  bool web_smoke_requested_ = false;
  bool window_destroyed_ = false;
  size_t live_browsers_ = 0;

  IMPLEMENT_REFCOUNTING(BrowserWindow);
};

}  // namespace shelter
