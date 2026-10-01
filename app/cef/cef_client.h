#pragma once
#include <string>
#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"
#include "app/browser/browser_controller.h"
namespace shelter {
class BridgeHandler;
class Client final:public CefClient,public CefLifeSpanHandler,public CefDisplayHandler,public CefLoadHandler {
 public:
  Client(BrowserController*,std::string);
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler()override{return this;}
  CefRefPtr<CefDisplayHandler> GetDisplayHandler()override{return this;}
  CefRefPtr<CefLoadHandler> GetLoadHandler()override{return this;}
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefProcessId,CefRefPtr<CefProcessMessage>)override;
  void OnAfterCreated(CefRefPtr<CefBrowser>)override;
  void OnBeforeClose(CefRefPtr<CefBrowser>)override;
  void OnTitleChange(CefRefPtr<CefBrowser>,const CefString&)override;
  void OnAddressChange(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,const CefString&)override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser>,bool,bool,bool)override;
  void OnLoadError(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,ErrorCode,const CefString&,const CefString&)override;
 private:
  BrowserController* controller_; std::string tab_id_; CefRefPtr<CefMessageRouterBrowserSide> router_;
  IMPLEMENT_REFCOUNTING(Client);
};
}
