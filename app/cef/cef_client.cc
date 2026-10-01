#include "app/cef/cef_client.h"
#include "app/common/logging.h"
#include "app/browser/browser_controller.h"
#include <utility>
namespace shelter { Client::Client(BrowserController* c,std::string id):controller_(c),tab_id_(std::move(id)){} void Client::OnTitleChange(CefRefPtr<CefBrowser>,const CefString& t){if(controller_)controller_->SetTitle(tab_id_,t.ToString());Log(LogLevel::Info,std::string("title: ")+t.ToString());} void Client::OnAddressChange(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame> f,const CefString& u){if(f&&f->IsMain()&&controller_)controller_->Navigate(tab_id_,u.ToString());} void Client::OnLoadingStateChange(CefRefPtr<CefBrowser>,bool l,bool b,bool f){if(controller_){controller_->SetLoading(tab_id_,l);auto*t=controller_->tabs().Find(tab_id_);if(t){t->can_go_back=b;t->can_go_forward=f;}}} void Client::OnLoadError(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,ErrorCode e,const CefString&,const CefString&){Log(LogLevel::Warning,"navigation error: "+std::to_string((int)e));} }
