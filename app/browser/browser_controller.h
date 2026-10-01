#pragma once
#include "app/browser/tab_manager.h"
#include "include/cef_browser.h"
#include <string_view>
namespace shelter {
class BrowserController {
 public:
  TabManager& tabs(){return tabs_;}
  bool Attach(std::string_view id, CefRefPtr<CefBrowser> browser);
  bool Navigate(std::string_view id,std::string_view url);
  bool GoBack(std::string_view id); bool GoForward(std::string_view id);
  bool Reload(std::string_view id); bool Stop(std::string_view id);
  bool SetLoading(std::string_view id,bool); bool SetTitle(std::string_view id,std::string_view);
  bool SetAddress(std::string_view id,std::string_view); bool SetHistoryState(std::string_view id,bool,bool);
 private: TabManager tabs_;
};
}
