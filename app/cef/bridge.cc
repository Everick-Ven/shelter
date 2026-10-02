#include "app/cef/bridge.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "app/cef/cef_app.h"
#include "app/common/logging.h"
#include "app/platform/platform.h"
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace shelter {
namespace {
bool HasPrefix(const std::string& s, const char* prefix) {
  const size_t n = strlen(prefix);
  return s.size() >= n && s.compare(0, n, prefix) == 0;
}
}  // namespace

std::string JsonQuote(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  out.push_back('"');
  for (const unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}

BridgeHandler::BridgeHandler(App* app) : app_(app) {}

bool BridgeHandler::OnQuery(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            int64_t query_id,
                            const CefString& request,
                            bool persistent,
                            CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  // Origin validation: only the SHELTER UI page may use the native bridge.
  const std::string frame_url = frame ? frame->GetURL().ToString() : std::string();
  if (!HasPrefix(frame_url, "shelter://ui")) {
    callback->Failure(403, "origin not allowed");
    return true;
  }

  CefRefPtr<CefValue> parsed = CefParseJSON(request, JSON_PARSER_RFC);
  if (!parsed.get() || parsed->GetType() != VTYPE_DICTIONARY) {
    callback->Failure(400, "bad request");
    return true;
  }
  CefRefPtr<CefDictionaryValue> msg = parsed->GetDictionary();
  const std::string cmd = msg->GetString("cmd").ToString();
  if (cmd.empty()) {
    callback->Failure(400, "missing cmd");
    return true;
  }

  if (cmd == "window:minimize" || cmd == "window:maximize" ||
      cmd == "window:close") {
    const std::string op = cmd.substr(7);  // after "window:"
    const bool ok = app_->WindowOp(op);
    callback->Success(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return true;
  }
  if (cmd == "tab:navigate") {
    const std::string tab_id = msg->GetString("tabId").ToString();
    const std::string url = msg->GetString("url").ToString();
    const bool reveal = msg->GetBool("reveal");
    if (app_->NavigateContent(tab_id, url, reveal)) {
      callback->Success("{\"ok\":true}");
    } else {
      callback->Failure(400, "navigation rejected");
    }
    return true;
  }
  if (cmd == "tab:hideContent") {
    app_->HideContent();
    callback->Success("{\"ok\":true}");
    return true;
  }
  if (cmd == "tab:stop") {
    const bool ok = app_->StopTab(msg->GetString("tabId").ToString());
    callback->Success(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return true;
  }
  if (cmd == "tab:reload") {
    const bool ok = app_->ReloadTab(msg->GetString("tabId").ToString());
    callback->Success(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return true;
  }
  if (cmd == "tab:zoom") {
    app_->ApplyZoom(msg->GetDouble("factor"));
    callback->Success("{\"ok\":true}");
    return true;
  }
  if (cmd == "tab:captureThumbs") {
    // Screenshot capture requires OSR paint output; not available in Views.
    callback->Success("{\"ok\":false,\"error\":\"capture-unavailable\"}");
    return true;
  }
  if (cmd == "viewport:sync") {
    CefRefPtr<CefDictionaryValue> rect = msg->GetDictionary("rect");
    CefRect bounds;
    if (rect) {
      bounds = CefRect(rect->GetInt("x"), rect->GetInt("y"),
                       rect->GetInt("width"), rect->GetInt("height"));
    }
    const bool visible = msg->GetBool("visible");
    const std::string active_tab = msg->GetString("activeTabId").ToString();
    app_->SyncViewport(bounds, visible, active_tab);
    callback->Success("{\"ok\":true}");
    return true;
  }
  if (cmd == "devtools:ctl") {
    const std::string act = msg->GetString("act").ToString();
    const std::string tab_id = msg->GetString("tabId").ToString();
    if (act == "close") {
      app_->CloseDevTools(tab_id);
      callback->Success("{\"ok\":true}");
      return true;
    }
    // act == "open" or "window": open/focus DevTools for a content tab.
    CefRect bounds;
    bool has_bounds = false;
    CefRefPtr<CefDictionaryValue> rect = msg->GetDictionary("bounds");
    if (rect) {
      bounds = CefRect(rect->GetInt("x"), rect->GetInt("y"),
                       rect->GetInt("width"), rect->GetInt("height"));
      has_bounds = bounds.width > 0 && bounds.height > 0;
    }
    const bool ok = app_->OpenDevTools(tab_id, bounds, has_bounds);
    callback->Success(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return true;
  }
  if (cmd == "theme:scheme") {
    // The UI applies themes itself; acknowledge for the native side.
    callback->Success("{\"ok\":true}");
    return true;
  }
  if (cmd == "privacy:clear") {
    // Best effort: drop all cookies from the global cookie store (an empty
    // URL deletes every cookie). Detailed per-category stats are not exposed
    // by the CEF public API, so the UI reports rows as unavailable.
    CefRefPtr<CefCookieManager> cookies =
        CefCookieManager::GetGlobalManager(nullptr);
    if (cookies) cookies->DeleteCookies(CefString(), CefString(), nullptr);
    callback->Success("{\"ok\":true}");
    return true;
  }
  if (cmd == "clipboard:write") {
    const std::string text = msg->GetString("text").ToString();
    const bool ok = PlatformClipboardWrite(text);
    callback->Success(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return true;
  }
  if (cmd == "clipboard:read") {
    std::optional<std::string> text = PlatformClipboardRead();
    std::string body = "{\"ok\":";
    body += text ? "true,\"text\":" : "false,\"text\":";
    body += JsonQuote(text.value_or(""));
    body += "}";
    callback->Success(body);
    return true;
  }
  if (cmd == "auth:open" || cmd == "ctx:act" || cmd == "window:openPlain") {
    // Popups/menus for third-party windows are not wired in the CEF shell yet;
    // the UI shows its own fallback (toast / simulated sign-in).
    callback->Success("{\"ok\":false,\"error\":\"unsupported\"}");
    return true;
  }
  if (cmd == "ai:test" || cmd == "ai:chat") {
    // No bundled LLM proxy: the UI falls back to its local reply engine.
    callback->Success("{\"ok\":false,\"error\":\"no-native-ai\"}");
    return true;
  }

  Log(LogLevel::Warning, "bridge: unknown cmd " + cmd);
  callback->Failure(404, "unknown cmd");
  return true;
}

}  // namespace shelter
