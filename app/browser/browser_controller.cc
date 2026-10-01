#include "app/browser/browser_controller.h"
#include "app/common/url_utils.h"
namespace shelter {
bool BrowserController::Attach(std::string_view id,CefRefPtr<CefBrowser> b){auto*t=tabs_.Find(id);if(!t||!b)return false;t->browser=b;return true;}
bool BrowserController::Navigate(std::string_view id,std::string_view u){auto*t=tabs_.Find(id);if(!t||!IsValidNavigationUrl(u)||!t->browser)return false;t->url=u;t->browser->GetMainFrame()->LoadURL(std::string(u));return true;}
bool BrowserController::GoBack(std::string_view id){auto*t=tabs_.Find(id);if(!t||!t->browser||!t->browser->CanGoBack())return false;t->browser->GoBack();return true;}
bool BrowserController::GoForward(std::string_view id){auto*t=tabs_.Find(id);if(!t||!t->browser||!t->browser->CanGoForward())return false;t->browser->GoForward();return true;}
bool BrowserController::Reload(std::string_view id){auto*t=tabs_.Find(id);if(!t||!t->browser)return false;t->browser->Reload();return true;}
bool BrowserController::Stop(std::string_view id){auto*t=tabs_.Find(id);if(!t||!t->browser)return false;t->browser->StopLoad();return true;}
bool BrowserController::SetLoading(std::string_view id,bool v){auto*t=tabs_.Find(id);if(!t)return false;t->loading=v;return true;}
bool BrowserController::SetTitle(std::string_view id,std::string_view v){auto*t=tabs_.Find(id);if(!t)return false;t->title=v;return true;}
bool BrowserController::SetAddress(std::string_view id,std::string_view v){auto*t=tabs_.Find(id);if(!t)return false;t->url=v;return true;}
bool BrowserController::SetHistoryState(std::string_view id,bool b,bool f){auto*t=tabs_.Find(id);if(!t)return false;t->can_go_back=b;t->can_go_forward=f;return true;}
}
