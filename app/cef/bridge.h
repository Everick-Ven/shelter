#pragma once
#include <string>
#include "include/wrapper/cef_message_router.h"
namespace shelter {
class App;
// Escapes |s| and returns it as a JSON string literal (including quotes).
std::string JsonQuote(const std::string& s);
// Handles window.cefQuery requests coming from the SHELTER UI page
// (shelter://ui/*). All commands are origin-validated inside OnQuery.
// NOTE: CefMessageRouterBrowserSide::Handler is a plain (non-refcounted)
// class; the owner (Client) must keep it alive while the router exists.
class BridgeHandler final : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit BridgeHandler(App* app);
  BridgeHandler(const BridgeHandler&) = delete;
  BridgeHandler& operator=(const BridgeHandler&) = delete;
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;
 private:
  App* app_;
};
}  // namespace shelter
