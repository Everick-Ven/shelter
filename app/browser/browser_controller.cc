#include "app/browser/browser_controller.h"
#include "app/common/url_utils.h"
namespace shelter {
bool BrowserController::Navigate(std::string_view id,std::string_view url){auto*t=tabs_.Find(id);if(!t||!IsValidNavigationUrl(url))return false;t->url=url;t->loading=true;return true;}
bool BrowserController::SetLoading(std::string_view id,bool v){auto*t=tabs_.Find(id);if(!t)return false;t->loading=v;return true;}
bool BrowserController::SetTitle(std::string_view id,std::string_view title){auto*t=tabs_.Find(id);if(!t)return false;t->title=title;return true;}
}
