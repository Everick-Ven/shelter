#pragma once
#include <map>
#include <string>
#include "app/browser/browser_controller.h"
#include "include/cef_app.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_message_router.h"
namespace shelter {
// Application shell. Owns the main window, the UI browser
// (shelter://ui/index.html) and one content CefBrowserView per web tab.
// Content views are positioned over the UI's #viewport rect reported via the
// "viewport:sync" bridge command; the UI page itself is the browser chrome.
class App final : public CefApp,
                  public CefBrowserProcessHandler,
                  public CefRenderProcessHandler {
 public:
  App();
  // CefApp
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }
  void OnBeforeCommandLineProcessing(const CefString&,
                                     CefRefPtr<CefCommandLine>) override;
  void OnContextInitialized() override;
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar>) override;
  // Renderer side of the message router (provides window.cefQuery).
  void OnContextCreated(CefRefPtr<CefBrowser>,
                        CefRefPtr<CefFrame>,
                        CefRefPtr<CefV8Context>) override;
  void OnContextReleased(CefRefPtr<CefBrowser>,
                         CefRefPtr<CefFrame>,
                         CefRefPtr<CefV8Context>) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>,
                                CefRefPtr<CefFrame>,
                                CefProcessId,
                                CefRefPtr<CefProcessMessage>) override;

  BrowserController& controller() { return controller_; }

  // Browser lifecycle (called from Client).
  void OnUiBrowserCreated(CefRefPtr<CefBrowser> browser);
  void OnUiBrowserClosed();
  void OnContentBrowserCreated(const std::string& tab_id,
                               CefRefPtr<CefBrowser> browser);
  void OnContentBrowserClosed(const std::string& tab_id);

  // Content browser events -> window.shelterCefDispatch(...) in the UI page.
  void OnContentTitle(const std::string& tab_id, const std::string& title);
  void OnContentAddress(const std::string& tab_id, const std::string& url);
  void OnContentLoading(const std::string& tab_id,
                        bool loading,
                        bool can_back,
                        bool can_forward);
  void OnContentLoadStart(const std::string& tab_id,
                          cef_transition_type_t transition);

  // Bridge API (UI thread).
  bool NavigateContent(const std::string& tab_id, const std::string& url,
                       bool reveal);
  void HideContent();
  void SyncViewport(const CefRect& rect, bool visible,
                    const std::string& active_tab);
  bool StopTab(const std::string& tab_id);
  bool ReloadTab(const std::string& tab_id);
  void ApplyZoom(double factor);
  bool WindowOp(const std::string& op);
  bool OpenDevTools(const std::string& tab_id, const CefRect& bounds,
                    bool has_bounds);
  void CloseDevTools(const std::string& tab_id);

  // Main window delegate hooks.
  void OnWindowCreated(CefRefPtr<CefWindow> window);
  void Relayout();
  bool RequestWindowClose();
  void OnWindowDestroyed();

  CefRect devtools_window_bounds() const { return devtools_bounds_; }
  bool has_devtools_window_bounds() const { return has_devtools_bounds_; }

 private:
  void DispatchToUi(const char* event, const std::string& json_data);
  void ShowContentTab(const std::string& tab_id);
  void HideAllContent();
  bool StartUiClose();
  void MaybeQuit();

  BrowserController controller_;

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> ui_view_;
  CefRefPtr<CefBrowser> ui_browser_;
  CefRefPtr<CefBrowserViewDelegate> view_delegate_;

  std::map<std::string, CefRefPtr<CefBrowserView>> content_;
  std::map<std::string, std::string> pending_urls_;

  CefRect viewport_rect_;
  std::string active_tab_;
  std::string shown_tab_;
  bool content_visible_ = false;
  double zoom_level_ = 0.0;
  bool closing_ = false;
  bool ui_close_started_ = false;
  int last_maximized_ = -1;  // -1 unknown, 0 normal, 1 maximized

  CefRect devtools_bounds_;
  bool has_devtools_bounds_ = false;

  CefRefPtr<CefMessageRouterRendererSide> renderer_router_;

  IMPLEMENT_REFCOUNTING(App);
};
}  // namespace shelter
