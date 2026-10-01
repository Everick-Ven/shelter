#include "app/browser/tab_manager.h"
namespace shelter {
TabState* TabManager::Find(std::string_view id){for(auto& t:tabs_)if(t.id==id)return &t;return nullptr;}
TabState* TabManager::Create(std::string id,std::string url){if(id.empty()||Find(id))return nullptr;TabState t; t.id=std::move(id); t.url=std::move(url); tabs_.push_back(std::move(t));active_=tabs_.size()-1;return &tabs_.back();}
bool TabManager::Close(std::string_view id){for(size_t i=0;i<tabs_.size();++i)if(tabs_[i].id==id){tabs_.erase(tabs_.begin()+i);if(tabs_.empty())active_.reset();else if(!active_||*active_>=tabs_.size())active_=tabs_.size()-1;return true;}return false;}
bool TabManager::Activate(std::string_view id){for(size_t i=0;i<tabs_.size();++i)if(tabs_[i].id==id){active_=i;return true;}return false;}
TabState* TabManager::Active(){return active_&&*active_<tabs_.size()?&tabs_[*active_]:nullptr;}
}
