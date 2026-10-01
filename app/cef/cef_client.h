#pragma once
#include <string>
#include <utility>
#include "include/cef_client.h"
#include "app/browser/browser_controller.h"
#include <string>
namespace shelter { class BrowserController;
class Client final : public CefClient, public CefLifeSpanHandler, public CefDisplayHandler, public CefLoadHandler { public: explicit Client(BrowserController* controller, std::string tab_id); CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override{return this;} CefRefPtr<CefDisplayHandler> GetDisplayHandler() override{return this;} CefRefPtr<CefLoadHandler> GetLoadHandler() override{return this;} void OnTitleChange(CefRefPtr<CefBrowser>,const CefString&) override;
  void OnAddressChange(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser>,bool,bool,bool) override; void OnAddressChange(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&) override; void OnLoadingStateChange(CefRefPtr<CefBrowser>,bool,bool,bool) override; void OnLoadError(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,ErrorCode,const CefString&,const CefString&) override; private: BrowserController* controller_; std::string tab_id_; IMPLEMENT_REFCOUNTING(Client); }; }
