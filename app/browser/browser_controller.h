#pragma once
#include "app/browser/tab_manager.h"
#include <string_view>
namespace shelter {
class BrowserController {
 public:
  TabManager& tabs(){return tabs_;}
  const TabManager& tabs()const{return tabs_;}
  bool Navigate(std::string_view id,std::string_view url);
  bool SetLoading(std::string_view id,bool loading);
  bool SetTitle(std::string_view id,std::string_view title);
 private: TabManager tabs_;
};
}
