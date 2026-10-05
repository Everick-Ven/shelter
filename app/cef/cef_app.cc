#include "app/cef/cef_app.h"

#include "app/browser/browser_window.h"
#include "app/common/logging.h"
#include "app/cef/ui_scheme.h"
#include "include/wrapper/cef_helpers.h"

namespace shelter {

void App::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  if (process_type.empty()) {
    command_line->AppendSwitch("disable-background-networking");
    command_line->AppendSwitch("no-default-browser-check");
  }
}

void App::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  registrar->AddCustomScheme("shelter", CEF_SCHEME_OPTION_STANDARD |
                                              CEF_SCHEME_OPTION_SECURE |
                                              CEF_SCHEME_OPTION_CORS_ENABLED);
}

void App::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  Log(LogLevel::Info, "CEF context initialized");
  RegisterUiScheme();
  browser_window_ = new BrowserWindow();
  browser_window_->Create();
}

void App::OnWebKitInitialized() {
  CEF_REQUIRE_RENDERER_THREAD();
  message_router_ = CefMessageRouterRendererSide::Create(CefMessageRouterConfig());
}

void App::OnContextCreated(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (message_router_) message_router_->OnContextCreated(browser, frame, context);
}

void App::OnContextReleased(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefV8Context> context) {
  CEF_REQUIRE_RENDERER_THREAD();
  if (message_router_) message_router_->OnContextReleased(browser, frame, context);
}

bool App::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefProcessId source_process,
                                   CefRefPtr<CefProcessMessage> message) {
  return message_router_ && message_router_->OnProcessMessageReceived(
                                browser, frame, source_process, message);
}

}  // namespace shelter
