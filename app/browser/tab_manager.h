#pragma once
#include <optional>
#include <string>
#include <vector>
namespace shelter {
struct TabState { std::string id; std::string url; std::string title; bool loading=false; bool can_go_back=false; bool can_go_forward=false; };
class TabManager {
 public:
  TabState* Create(std::string id, std::string url);
  bool Close(std::string_view id);
  bool Activate(std::string_view id);
  TabState* Active();
  TabState* Find(std::string_view id);
  const std::vector<TabState>& All() const { return tabs_; }
 private: std::vector<TabState> tabs_; std::optional<size_t> active_;
};
}
