#pragma once

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_process_message.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_message_router.h"
#include "app/browser/browser_window.h"

namespace shelter {

class App final : public CefApp,
                  public CefBrowserProcessHandler,
                  public CefRenderProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;
  void OnContextInitialized() override;
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
  void OnWebKitInitialized() override;
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  CefRefPtr<BrowserWindow> browser_window_;
  CefRefPtr<CefMessageRouterRendererSide> message_router_;

  IMPLEMENT_REFCOUNTING(App);
};

}  // namespace shelter
