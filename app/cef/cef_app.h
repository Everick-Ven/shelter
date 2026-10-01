#pragma once
#include "include/cef_app.h"
#include "app/browser/browser_controller.h"
namespace shelter { class App final:public CefApp,public CefBrowserProcessHandler,public CefRenderProcessHandler { public: CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler()override{return this;} CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler()override{return this;} void OnBeforeCommandLineProcessing(const CefString&,CefRefPtr<CefCommandLine>)override; void OnContextInitialized()override; void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar>) override; private: BrowserController controller_; IMPLEMENT_REFCOUNTING(App); }; }
