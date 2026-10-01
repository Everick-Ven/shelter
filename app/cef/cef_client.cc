#include "app/cef/cef_client.h"
#include "app/common/logging.h"
#include "include/cef_parser.h"
#include <utility>
namespace shelter {
Client::Client(BrowserController* c,std::string id):controller_(c),tab_id_(std::move(id)),router_(CefMessageRouterBrowserSide::Create(CefMessageRouterConfig())){}
void Client::OnAfterCreated(CefRefPtr<CefBrowser> b){if(controller_)controller_->Attach(tab_id_,b);}
void Client::OnBeforeClose(CefRefPtr<CefBrowser> b){if(router_)router_->OnBeforeClose(b);}
bool Client::OnProcessMessageReceived(CefRefPtr<CefBrowser>b,CefRefPtr<CefFrame>f,CefProcessId p,CefRefPtr<CefProcessMessage>m){return router_&&router_->OnProcessMessageReceived(b,f,p,m);}
void Client::OnTitleChange(CefRefPtr<CefBrowser>,const CefString&t){if(controller_)controller_->SetTitle(tab_id_,t.ToString());Log(LogLevel::Info,std::string("title: ")+t.ToString());}
void Client::OnAddressChange(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>f,const CefString&u){if(f&&f->IsMain()&&controller_)controller_->SetAddress(tab_id_,u.ToString());}
void Client::OnLoadingStateChange(CefRefPtr<CefBrowser>,bool l,bool b,bool f){if(controller_){controller_->SetLoading(tab_id_,l);controller_->SetHistoryState(tab_id_,b,f);}}
void Client::OnLoadError(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,ErrorCode e,const CefString&,const CefString&){Log(LogLevel::Warning,"navigation error: "+std::to_string((int)e));}
}
