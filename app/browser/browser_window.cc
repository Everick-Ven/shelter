#include "app/browser/browser_window.h"

#include "app/cef/cef_client.h"
#include "app/common/logging.h"
#include "app/common/url_utils.h"
#include "include/cef_app.h"
#include "include/cef_frame.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_values.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <spawn.h>
#include <sys/types.h>
extern char** environ;
#endif

namespace shelter {
namespace {
constexpr char kUiUrl[] = "shelter://ui/index.html";

class DeferredBrowserTask final : public CefTask {
 public:
  explicit DeferredBrowserTask(std::function<void()> callback)
      : callback_(std::move(callback)) {}

  void Execute() override {
    if (callback_) callback_();
  }

 private:
  std::function<void()> callback_;

  IMPLEMENT_REFCOUNTING(DeferredBrowserTask);
};

class AlloyBrowserViewDelegate final : public CefBrowserViewDelegate {
 public:
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  IMPLEMENT_REFCOUNTING(AlloyBrowserViewDelegate);
};

bool IsWebUrl(const std::string& url) {
  if (!IsValidNavigationUrl(url)) return false;
  const size_t separator = url.find("://");
  if (separator == std::string::npos) return false;
  std::string scheme = url.substr(0, separator);
  std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return scheme == "http" || scheme == "https";
}

std::string PathToUtf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

CefRefPtr<CefDictionaryValue> NewDictionary() {
  return CefDictionaryValue::Create();
}

double GetNumber(CefRefPtr<CefDictionaryValue> dictionary, const char* key) {
  if (!dictionary || !dictionary->HasKey(key)) return 0.0;
  switch (dictionary->GetType(key)) {
    case VTYPE_INT:
      return static_cast<double>(dictionary->GetInt(key));
    case VTYPE_DOUBLE:
      return dictionary->GetDouble(key);
    default:
      return 0.0;
  }
}

bool RevealFileInManager(const std::string& path_utf8) {
  if (path_utf8.empty()) return false;
  const std::filesystem::path path = std::filesystem::u8path(path_utf8);
#if defined(_WIN32)
  const std::wstring arguments = L"/select,\"" + path.wstring() + L"\"";
  return reinterpret_cast<INT_PTR>(ShellExecuteW(
             nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr,
             SW_SHOWNORMAL)) > 32;
#elif defined(__APPLE__)
  const std::string native_path = path.string();
  char open_arg[] = "open";
  char reveal_arg[] = "-R";
  char* argv[] = {open_arg, reveal_arg, const_cast<char*>(native_path.c_str()), nullptr};
  pid_t child = 0;
  return posix_spawnp(&child, "open", nullptr, nullptr, argv, environ) == 0;
#else
  return false;
#endif
}

class ShellWindowDelegate final : public CefWindowDelegate {
 public:
  ShellWindowDelegate(CefRefPtr<BrowserWindow> owner,
                      CefRefPtr<CefBrowserView> ui_view)
      : owner_(std::move(owner)), ui_view_(std::move(ui_view)) {}

  bool IsFrameless(CefRefPtr<CefWindow>) override { return false; }

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetTitle("SHELTER");

    // AddOverlayView creates a root-level z-order reference view. A FillLayout
    // stretches that reference across the whole window and lets it intercept
    // hit testing, which can leave native CEF web-tab overlays hidden behind
    // the UI on macOS. Keep the reference at zero width and let only the UI
    // browser view consume the window's horizontal space.
    CefBoxLayoutSettings layout_settings;
    layout_settings.horizontal = true;
    layout_settings.inside_border_insets = CefInsets(0, 0, 0, 0);
    layout_settings.between_child_spacing = 0;
    layout_settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
    layout_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
    layout_settings.minimum_cross_axis_size = 0;
    layout_settings.default_flex = 0;
    CefRefPtr<CefBoxLayout> layout = window->SetToBoxLayout(layout_settings);
    window->AddChildView(ui_view_);
    if (layout) layout->SetFlexForView(ui_view_, 1);

    window->SetBounds(CefRect(100, 80, 1280, 820));
    window->Show();
    ui_view_->RequestFocus();
  }

  bool CanClose(CefRefPtr<CefWindow>) override { return true; }

  cef_runtime_style_t GetWindowRuntimeStyle() override {
    // The SHELTER window contains one UI BrowserView plus native BrowserViews
    // for web tabs. Chrome-style windows reject additional BrowserViews.
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  void OnWindowClosing(CefRefPtr<CefWindow> window) override {
    if (owner_) owner_->OnWindowClosing(window);
  }

  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    if (owner_) owner_->OnWindowDestroyed(window);
  }

 private:
  CefRefPtr<BrowserWindow> owner_;
  CefRefPtr<CefBrowserView> ui_view_;

  IMPLEMENT_REFCOUNTING(ShellWindowDelegate);
};

}  // namespace

BrowserWindow::BrowserWindow() = default;

void BrowserWindow::Create() {
  CEF_REQUIRE_UI_THREAD();
  CefBrowserSettings settings;
  ui_view_ = CefBrowserView::CreateBrowserView(
      new Client(this, nullptr, std::string(), true), kUiUrl, settings,
      nullptr, nullptr, new AlloyBrowserViewDelegate());
  if (!ui_view_) {
    Log(LogLevel::Error, "Failed to create the SHELTER UI browser view");
    CefQuitMessageLoop();
    return;
  }

  window_ = CefWindow::CreateTopLevelWindow(
      new ShellWindowDelegate(this, ui_view_));
  if (!window_) {
    Log(LogLevel::Error, "Failed to create the SHELTER top-level window");
    CefQuitMessageLoop();
  }
}

void BrowserWindow::OnWindowClosing(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || window_ != window) return;

  content_visible_ = false;
  for (auto& [tab_id, web_tab] : web_tabs_) {
    web_tab.closing = true;
    if (web_tab.overlay) web_tab.overlay->SetVisible(false);
    auto tab = controller_.tabs().Find(tab_id);
    if (tab && tab->browser) tab->browser->GetHost()->CloseBrowser(true);
  }

  // Releasing an uncontinued CefBeforeDownloadCallback cancels its download.
  pending_downloads_.clear();
  if (ui_browser_) ui_browser_->GetHost()->CloseBrowser(true);
}

void BrowserWindow::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  if (window_ && window_ == window) window_ = nullptr;
  window_destroyed_ = true;
  ui_view_ = nullptr;
  MaybeQuitMessageLoop();
}

void BrowserWindow::OnUiBrowserCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  ++live_browsers_;
  if (window_destroyed_) {
    if (browser) browser->GetHost()->CloseBrowser(true);
    return;
  }
  ui_browser_ = browser;
}

void BrowserWindow::OnUiLoadEnd() {
  CEF_REQUIRE_UI_THREAD();
  auto data = NewDictionary();
  data->SetString("engine", "CEF");
  DispatchToUi("engine-ready", data);

  // The packaged-app runtime test sets this variable to exercise the complete
  // UI -> native bridge -> CEF web-tab path against a real HTTPS document.
  const char* smoke_url = std::getenv("SHELTER_WEB_SMOKE_URL");
  if (web_smoke_requested_ || !smoke_url || !*smoke_url ||
      !IsWebUrl(smoke_url) || !ui_browser_ || !ui_browser_->GetMainFrame()) {
    return;
  }

  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetString(std::string(smoke_url));
  const std::string json = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  const std::string script =
      "(function() {"
      "  if (typeof window.openPage !== 'function' || !window.shelterCef) return;"
      "  window.openPage('dashboard');"
      "  window.setTimeout(function() {"
      "    const viewport = document.getElementById('viewport');"
      "    const page = document.getElementById('page');"
      "    const top = document.querySelector('.dash-top');"
      "    let overflowPx = 0;"
      "    if (viewport) {"
      "      const edge = viewport.getBoundingClientRect().right;"
      "      document.querySelectorAll('.dash-top > *, .dash-right > *, .dash-bottom > *').forEach(function(card) {"
      "        overflowPx = Math.max(overflowPx, card.getBoundingClientRect().right - edge);"
      "      });"
      "    }"
      "    if (page) overflowPx = Math.max(overflowPx, page.scrollWidth - page.clientWidth);"
      "    if (top) overflowPx = Math.max(overflowPx, top.scrollWidth - top.clientWidth);"
      "    overflowPx = Math.max(0, overflowPx);"
      "    const passed = Boolean(viewport && page && top && overflowPx <= 1);"
      "    const result = window.shelterCef.send('runtime:dashboard-layout', {"
      "      passed: passed, overflowPx: overflowPx,"
      "      viewportWidth: viewport ? viewport.clientWidth : 0"
      "    });"
      "    Promise.resolve(result).finally(function() {"
      "      window.setTimeout(function() {"
      "        if (window.shelter && typeof window.shelter.navigate === 'function')"
      "          window.shelter.navigate(" + json + ");"
      "      }, 100);"
      "    });"
      "  }, 750);"
      "})();";
  web_smoke_requested_ = true;
  Log(LogLevel::Info, "SHELTER_WEB_SMOKE_REQUESTED url=" + std::string(smoke_url));
  ui_browser_->GetMainFrame()->ExecuteJavaScript(script, kUiUrl, 0);
}

void BrowserWindow::OnUiBrowserClosed(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (ui_browser_ && browser && ui_browser_->GetIdentifier() == browser->GetIdentifier()) {
    ui_browser_ = nullptr;
  }
  if (live_browsers_ > 0) --live_browsers_;
  MaybeQuitMessageLoop();
}

void BrowserWindow::OnWebBrowserCreated(const std::string& tab_id,
                                        CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) return;

  auto it = web_tabs_.find(tab_id);
  if (it == web_tabs_.end()) {
    // A tab removed while CEF was still creating its browser must not survive.
    ++live_browsers_;
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  if (it->second.browser) {
    if (it->second.browser->GetIdentifier() == browser->GetIdentifier()) {
      UpdateWebTabBoundsAndVisibility();
      return;
    }
    browser->GetHost()->CloseBrowser(true);
    return;
  }

  it->second.browser = browser;
  ++live_browsers_;
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info,
        "SHELTER_WEB_SMOKE_BROWSER_CREATED tab=" + tab_id +
            " browser_id=" + std::to_string(browser->GetIdentifier()) +
            " url=" +
            (browser->GetMainFrame()
                 ? browser->GetMainFrame()->GetURL().ToString()
                 : "unavailable"));
  }
  if (window_destroyed_ || !window_ || it->second.closing) {
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  if (!it->second.pending_url.empty()) {
    const std::string pending_url = std::move(it->second.pending_url);
    it->second.pending_url.clear();
    controller_.Navigate(tab_id, pending_url);
  }
  UpdateWebTabBoundsAndVisibility();
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info,
        "SHELTER_WEB_SMOKE_BROWSER_CREATED_CALLBACK_END tab=" + tab_id);
  }
}

void BrowserWindow::OnWebBrowserClosed(const std::string& tab_id,
                                       CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info,
        "SHELTER_WEB_SMOKE_BROWSER_CLOSED tab=" + tab_id +
            " browser_id=" +
            (browser ? std::to_string(browser->GetIdentifier()) : "unknown"));
  }
  if (live_browsers_ > 0) --live_browsers_;
  auto it = web_tabs_.find(tab_id);
  if (it == web_tabs_.end()) {
    MaybeQuitMessageLoop();
    return;
  }
  if (it->second.overlay && !window_destroyed_) it->second.overlay->Destroy();
  web_tabs_.erase(it);
  controller_.tabs().Close(tab_id);
  if (active_tab_id_ == tab_id) active_tab_id_.clear();
  if (!window_destroyed_) UpdateWebTabBoundsAndVisibility();
  MaybeQuitMessageLoop();
}

void BrowserWindow::OnWebAddressChanged(const std::string& tab_id,
                                        const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  controller_.SetAddress(tab_id, url);
  DispatchWebTabState(tab_id);
}

void BrowserWindow::OnWebTitleChanged(const std::string& tab_id,
                                      const std::string& title) {
  CEF_REQUIRE_UI_THREAD();
  controller_.SetTitle(tab_id, title);
  auto data = NewDictionary();
  data->SetString("tabId", tab_id);
  data->SetString("title", title);
  DispatchToUiAsync("title", data);
}

void BrowserWindow::OnWebLoadingChanged(const std::string& tab_id,
                                        bool loading,
                                        bool can_go_back,
                                        bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  controller_.SetLoading(tab_id, loading);
  controller_.SetHistoryState(tab_id, can_go_back, can_go_forward);
  auto data = NewDictionary();
  data->SetString("tabId", tab_id);
  data->SetBool("loading", loading);
  data->SetBool("canGoBack", can_go_back);
  data->SetBool("canGoForward", can_go_forward);
  DispatchToUiAsync("loading", data);
}

void BrowserWindow::OnWebFullscreenChanged(CefRefPtr<CefBrowser> browser,
                                           bool fullscreen) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !window_->IsValid()) return;
  window_->SetFullscreen(fullscreen);
  auto data = NewDictionary();
  data->SetBool("fullscreen", fullscreen);
  data->SetBool("maximized", window_->IsMaximized());
  DispatchToUiAsync("window-state", data);
}

void BrowserWindow::OnWebBeforeDownload(
    CefRefPtr<CefDownloadItem> item,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!item || !callback) return;
  if (!ui_browser_) return;

  const int id = item->GetId();
  PendingDownload pending;
  pending.callback = callback;
  pending.filename = item->GetSuggestedFileName().ToString();
  pending_downloads_[id] = std::move(pending);

  auto data = NewDictionary();
  data->SetInt("id", id);
  data->SetString("filename", item->GetSuggestedFileName());
  data->SetString("url", item->GetURL());
  data->SetDouble("size", static_cast<double>(item->GetTotalBytes()));
  DispatchToUiAsync("download-prompt", data);
}

void BrowserWindow::OnWebDownloadUpdated(CefRefPtr<CefDownloadItem> item) {
  CEF_REQUIRE_UI_THREAD();
  if (!item) return;

  std::string state = "progressing";
  if (item->IsComplete()) state = "completed";
  else if (item->IsCanceled()) state = "cancelled";
  else if (item->IsInterrupted()) state = "interrupted";

  auto data = NewDictionary();
  data->SetInt("id", item->GetId());
  data->SetString("filename", item->GetSuggestedFileName());
  data->SetString("url", item->GetURL());
  data->SetString("path", item->GetFullPath());
  data->SetString("state", state);
  data->SetDouble("total", static_cast<double>(item->GetTotalBytes()));
  data->SetDouble("bytes", static_cast<double>(item->GetReceivedBytes()));
  DispatchToUiAsync("download", data);

  if (item->IsComplete() || item->IsCanceled() || item->IsInterrupted()) {
    pending_downloads_.erase(item->GetId());
  }
}

void BrowserWindow::OpenPopupInNewTab(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!IsWebUrl(url) || !ui_browser_ || !ui_browser_->GetMainFrame()) return;
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetString(url);
  const std::string json = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  const std::string script = "if (typeof window.newTab === 'function') window.newTab(" +
                             json + ");";
  ui_browser_->GetMainFrame()->ExecuteJavaScript(script, kUiUrl, 0);
}

void BrowserWindow::CloseWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (window_ && window_->IsValid()) window_->Close();
}

void BrowserWindow::MaybeQuitMessageLoop() {
  if (window_destroyed_ && live_browsers_ == 0) CefQuitMessageLoop();
}

bool BrowserWindow::HandleBridgeCommand(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const std::string& command,
    CefRefPtr<CefDictionaryValue> arguments,
    std::string& response) {
  CEF_REQUIRE_UI_THREAD();
  response = "{\"ok\":false,\"unsupported\":true}";
  if (!IsTrustedUiFrame(browser, frame)) return false;
  if (!arguments) arguments = NewDictionary();

  auto get_string = [&arguments](const char* key) {
    return arguments->HasKey(key) ? arguments->GetString(key).ToString() : std::string();
  };
  const std::string tab_id = get_string("tabId");

  if (command == "runtime:dashboard-layout") {
    if (!std::getenv("SHELTER_WEB_SMOKE_URL")) return false;
    const bool passed = arguments->HasKey("passed") &&
                        arguments->GetType("passed") == VTYPE_BOOL &&
                        arguments->GetBool("passed");
    const double overflow_px = GetNumber(arguments, "overflowPx");
    const double viewport_width = GetNumber(arguments, "viewportWidth");
    Log(LogLevel::Info,
        std::string("SHELTER_DASHBOARD_LAYOUT_") +
            (passed ? "PASS" : "FAIL") +
            " overflow_px=" + std::to_string(overflow_px) +
            " viewport_width=" + std::to_string(viewport_width));
    response = passed ? "{\"ok\":true}" : "{\"ok\":false}";
    return true;
  }

  if (command == "viewport:sync") {
    active_tab_id_ = get_string("activeTabId");
    const bool visible = arguments->GetBool("visible");
    if (arguments->HasKey("rect") &&
        arguments->GetType("rect") == VTYPE_DICTIONARY) {
      auto rect = arguments->GetDictionary("rect");
      if (rect) {
        const int x = static_cast<int>(GetNumber(rect, "x"));
        const int y = static_cast<int>(GetNumber(rect, "y"));
        const int width = std::max(0, static_cast<int>(GetNumber(rect, "width")));
        const int height = std::max(0, static_cast<int>(GetNumber(rect, "height")));
        viewport_bounds_ = CefRect(x, y, width, height);
      }
    }
    content_visible_ = visible;
    UpdateWebTabBoundsAndVisibility();
    response = "{\"ok\":true}";
    return true;
  }

  if (command == "tab:navigate") {
    const std::string url = get_string("url");
    if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
      Log(LogLevel::Info,
          "SHELTER_WEB_SMOKE_NAVIGATE tab=" + tab_id + " url=" + url);
    }
    if (tab_id.empty() || !IsWebUrl(url)) return false;

    // Creating/attaching a BrowserView can synchronously trigger CEF load
    // callbacks. Defer it until the bridge query has returned to the renderer;
    // dispatching JavaScript back to that renderer while it is waiting on this
    // query can deadlock inside AddOverlayView.
    CefRefPtr<BrowserWindow> self(this);
    CefRefPtr<CefTask> task = new DeferredBrowserTask(
        [self, tab_id, url] { self->NavigateWebTab(tab_id, url); });
    if (!CefPostTask(TID_UI, task)) {
      Log(LogLevel::Error, "Failed to queue native browser navigation");
      return false;
    }
    if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
      Log(LogLevel::Info,
          "SHELTER_WEB_SMOKE_NAVIGATION_QUEUED tab=" + tab_id +
              " url=" + url);
    }
    response = "{\"ok\":true}";
    return true;
  }

  if (command == "tab:hideContent") {
    content_visible_ = false;
    UpdateWebTabBoundsAndVisibility();
    response = "{\"ok\":true}";
    return true;
  }

  if (command == "tab:stop") {
    const std::string target = tab_id.empty() ? active_tab_id_ : tab_id;
    controller_.Stop(target);
    response = "{\"ok\":true}";
    return true;
  }

  if (command == "tab:back" || command == "tab:forward" ||
      command == "tab:reload") {
    const std::string target = tab_id.empty() ? active_tab_id_ : tab_id;
    bool ok = false;
    if (command == "tab:back") ok = controller_.GoBack(target);
    else if (command == "tab:forward") ok = controller_.GoForward(target);
    else ok = controller_.Reload(target);
    response = ok ? "{\"ok\":true}" : "{\"ok\":false}";
    return true;
  }

  if (command == "tab:zoom") {
    const std::string target = tab_id.empty() ? active_tab_id_ : tab_id;
    const double factor = GetNumber(arguments, "factor");
    auto tab = controller_.tabs().Find(target);
    if (tab && tab->browser) {
      const double safe_factor = std::clamp(factor, 0.5, 3.0);
      tab->browser->GetHost()->SetZoomLevel(std::log(safe_factor) / std::log(1.2));
      response = "{\"ok\":true}";
      return true;
    }
    return false;
  }

  if (command == "tab:close") {
    CloseWebTab(tab_id.empty() ? active_tab_id_ : tab_id);
    response = "{\"ok\":true}";
    return true;
  }

  if (command == "download:decision") {
    const int id = arguments->GetInt("id");
    const std::string action = get_string("action");
    ContinueDownload(id, action);
    response = "{\"ok\":true}";
    return true;
  }

  if (command == "file:showInFolder") {
    response = RevealFileInManager(get_string("path"))
                   ? "{\"ok\":true}"
                   : "{\"ok\":false}";
    return true;
  }

  if (command == "window:minimize") {
    if (window_ && window_->IsValid()) window_->Minimize();
    response = "{\"ok\":true}";
    return true;
  }
  if (command == "window:maximize") {
    if (window_ && window_->IsValid()) {
      if (window_->IsMaximized()) window_->Restore();
      else window_->Maximize();
    }
    response = "{\"ok\":true}";
    return true;
  }
  if (command == "window:close") {
    CloseWindow();
    response = "{\"ok\":true}";
    return true;
  }

  return true;
}

void BrowserWindow::NavigateWebTab(std::string tab_id, std::string url) {
  CEF_REQUIRE_UI_THREAD();
  if (window_destroyed_ || !window_ || !window_->IsValid()) return;
  if (!EnsureWebTab(tab_id, url)) {
    Log(LogLevel::Error, "Failed to create native web tab " + tab_id);
    return;
  }
  active_tab_id_ = std::move(tab_id);
  content_visible_ = true;
  UpdateWebTabBoundsAndVisibility();
}

bool BrowserWindow::EnsureWebTab(const std::string& tab_id,
                                 const std::string& url) {
  if (!window_ || !window_->IsValid()) return false;

  auto it = web_tabs_.find(tab_id);
  if (it != web_tabs_.end()) {
    if (it->second.closing) return false;
    auto tab = controller_.tabs().Find(tab_id);
    if (tab && tab->browser) {
      if (tab->url == url) return true;
      it->second.requested_url = url;
      return controller_.Navigate(tab_id, url);
    }
    if (it->second.requested_url != url) {
      it->second.requested_url = url;
      it->second.pending_url = url;
    }
    return true;
  }

  if (!controller_.tabs().Find(tab_id)) {
    if (!controller_.tabs().Create(tab_id, url)) return false;
  } else {
    controller_.SetAddress(tab_id, url);
  }
  controller_.tabs().Activate(tab_id);

  // Register the tab before creating/attaching its BrowserView. CEF may deliver
  // OnAfterCreated synchronously during either call; the client callback must
  // be able to associate that browser instead of treating it as an orphan.
  WebTab web_tab;
  web_tab.requested_url = url;
  auto [web_tab_it, inserted] = web_tabs_.emplace(tab_id, std::move(web_tab));
  if (!inserted) return false;

  CefBrowserSettings settings;
  // Avoid starting a document load while CEF is attaching the BrowserView.
  // The requested URL is loaded explicitly once AddOverlayView completes.
  auto view = CefBrowserView::CreateBrowserView(
      new Client(this, &controller_, tab_id, false), "", settings,
      nullptr, nullptr, new AlloyBrowserViewDelegate());
  if (!view) {
    web_tabs_.erase(web_tab_it);
    controller_.tabs().Close(tab_id);
    return false;
  }
  web_tab_it->second.view = view;
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info, "SHELTER_WEB_SMOKE_VIEW_CREATED tab=" + tab_id);
    Log(LogLevel::Info, "SHELTER_WEB_SMOKE_OVERLAY_ADD_BEGIN tab=" + tab_id);
  }
  auto overlay = window_->AddOverlayView(view, CEF_DOCKING_MODE_CUSTOM, true);
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info,
        "SHELTER_WEB_SMOKE_OVERLAY_ADD_END tab=" + tab_id +
            " success=" + (overlay ? "true" : "false"));
  }
  if (!overlay) {
    if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
      Log(LogLevel::Error,
          "SHELTER_WEB_SMOKE_OVERLAY_ADD_FAILED tab=" + tab_id);
    }
    if (auto browser = view->GetBrowser()) browser->GetHost()->CloseBrowser(true);
    web_tabs_.erase(web_tab_it);
    controller_.tabs().Close(tab_id);
    return false;
  }

  web_tab_it->second.overlay = overlay;
  overlay->SetBounds(viewport_bounds_);
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info, "SHELTER_WEB_SMOKE_OVERLAY_BOUNDS_SET tab=" + tab_id);
  }
  overlay->SetVisible(false);
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info, "SHELTER_WEB_SMOKE_OVERLAY_HIDDEN tab=" + tab_id);
  }
  UpdateWebTabBoundsAndVisibility();
  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info,
        "SHELTER_WEB_SMOKE_OVERLAY_VISIBILITY_UPDATED tab=" + tab_id);
  }

  // Start remote navigation only after the BrowserView is attached to the
  // window. If CEF has not delivered OnAfterCreated yet, retain the URL for
  // OnWebBrowserCreated to load once the browser is ready.
  bool navigation_started = false;
  if (web_tab_it->second.browser) {
    if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
      Log(LogLevel::Info,
          "SHELTER_WEB_SMOKE_LOAD_URL_BEGIN tab=" + tab_id +
              " source=controller url=" + url);
    }
    navigation_started = controller_.Navigate(tab_id, url);
    if (!navigation_started &&
        web_tab_it->second.browser->GetMainFrame()) {
      if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
        Log(LogLevel::Warning,
            "SHELTER_WEB_SMOKE_LOAD_URL_FALLBACK tab=" + tab_id);
      }
      web_tab_it->second.browser->GetMainFrame()->LoadURL(url);
      navigation_started = true;
    }
    if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
      Log(LogLevel::Info,
          "SHELTER_WEB_SMOKE_LOAD_URL_END tab=" + tab_id +
              " started=" +
              (navigation_started ? "true" : "false"));
    }
  } else {
    web_tab_it->second.pending_url = url;
    navigation_started = true;
    if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
      Log(LogLevel::Info,
          "SHELTER_WEB_SMOKE_LOAD_URL_PENDING tab=" + tab_id + " url=" + url);
    }
  }

  if (std::getenv("SHELTER_WEB_SMOKE_URL")) {
    Log(LogLevel::Info,
        "SHELTER_WEB_SMOKE_OVERLAY_ATTACHED tab=" + tab_id +
            " visible=" +
            (content_visible_ && active_tab_id_ == tab_id ? "true" : "false") +
            " bounds=" + std::to_string(viewport_bounds_.x) + "," +
            std::to_string(viewport_bounds_.y) + "," +
            std::to_string(viewport_bounds_.width) + "," +
            std::to_string(viewport_bounds_.height) +
            " browser_attached=" +
            (web_tab_it->second.browser ? "true" : "false") +
            " navigation_started=" +
            (navigation_started ? "true" : "false") + " url=" + url);
  }
  return true;
}

void BrowserWindow::SetWebContentVisible(bool visible) {
  content_visible_ = visible;
  UpdateWebTabBoundsAndVisibility();
}

void BrowserWindow::UpdateWebTabBoundsAndVisibility() {
  for (auto& [tab_id, web_tab] : web_tabs_) {
    if (!web_tab.overlay) continue;
    web_tab.overlay->SetBounds(viewport_bounds_);
    web_tab.overlay->SetVisible(content_visible_ && tab_id == active_tab_id_ &&
                                !web_tab.closing && viewport_bounds_.width > 0 &&
                                viewport_bounds_.height > 0);
  }
}

void BrowserWindow::CloseWebTab(const std::string& tab_id) {
  auto it = web_tabs_.find(tab_id);
  if (it == web_tabs_.end()) {
    controller_.tabs().Close(tab_id);
    return;
  }
  it->second.closing = true;
  if (it->second.overlay) it->second.overlay->SetVisible(false);
  auto tab = controller_.tabs().Find(tab_id);
  if (tab && tab->browser) {
    tab->browser->GetHost()->CloseBrowser(true);
  } else {
    it->second.pending_url.clear();
  }
  if (active_tab_id_ == tab_id) active_tab_id_.clear();
  UpdateWebTabBoundsAndVisibility();
}

void BrowserWindow::DispatchToUi(const std::string& event,
                                 CefRefPtr<CefDictionaryValue> data) {
  if (!ui_browser_ || !ui_browser_->GetMainFrame() ||
      !ui_browser_->GetMainFrame()->IsValid()) {
    return;
  }
  if (!data) data = NewDictionary();
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(data);
  const std::string json = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  const std::string script =
      std::string("if (typeof window.shelterCefDispatch === 'function') ") +
      "window.shelterCefDispatch('" + event + "'," + json + ");";
  ui_browser_->GetMainFrame()->ExecuteJavaScript(script, kUiUrl, 0);
}

void BrowserWindow::DispatchToUiAsync(std::string event,
                                      CefRefPtr<CefDictionaryValue> data) {
  CefRefPtr<BrowserWindow> self(this);
  CefRefPtr<CefTask> task = new DeferredBrowserTask(
      [self, event = std::move(event), data = std::move(data)] {
        if (!self->window_destroyed_) self->DispatchToUi(event, data);
      });
  if (!CefPostTask(TID_UI, task)) {
    Log(LogLevel::Warning, "Failed to queue SHELTER UI event: " + event);
  }
}

void BrowserWindow::DispatchWebTabState(const std::string& tab_id) {
  auto tab = controller_.tabs().Find(tab_id);
  if (!tab) return;
  auto data = NewDictionary();
  data->SetString("tabId", tab_id);
  data->SetString("url", tab->url);
  data->SetBool("canGoBack", tab->browser && tab->browser->CanGoBack());
  data->SetBool("canGoForward", tab->browser && tab->browser->CanGoForward());
  DispatchToUiAsync("url", data);
}

bool BrowserWindow::IsTrustedUiFrame(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame) const {
  if (!browser || !frame || !frame->IsMain() || !ui_browser_ ||
      browser->GetIdentifier() != ui_browser_->GetIdentifier()) {
    return false;
  }
  const std::string url = frame->GetURL().ToString();
  return url.rfind("shelter://ui/", 0) == 0;
}

void BrowserWindow::ContinueDownload(int download_id,
                                     const std::string& action) {
  auto it = pending_downloads_.find(download_id);
  if (it == pending_downloads_.end() || !it->second.callback) return;
  auto callback = it->second.callback;
  const std::string filename = it->second.filename;
  pending_downloads_.erase(it);

  if (action == "cancel") return;
  if (action == "saveAs") {
    callback->Continue(filename, true);
    return;
  }
  if (action != "save") return;
  callback->Continue(GetUniqueDownloadPath(filename, download_id), false);
}

std::string BrowserWindow::GetDownloadsDirectory() const {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  PWSTR known_folder = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE,
                                     nullptr, &known_folder)) && known_folder) {
    const fs::path path(known_folder);
    CoTaskMemFree(known_folder);
    std::error_code error;
    fs::create_directories(path, error);
    return PathToUtf8(path);
  }
#endif
  const char* home = std::getenv("HOME");
#if defined(_WIN32)
  if (!home) home = std::getenv("USERPROFILE");
#endif
  fs::path path = home ? fs::u8path(home) / "Downloads"
                       : fs::temp_directory_path() / "SHELTER Downloads";
  std::error_code error;
  fs::create_directories(path, error);
  return PathToUtf8(path);
}

std::string BrowserWindow::GetUniqueDownloadPath(const std::string& filename,
                                                int download_id) const {
  namespace fs = std::filesystem;
  std::string safe_name = filename;
  std::replace(safe_name.begin(), safe_name.end(), '\\', '/');
  fs::path name = fs::u8path(safe_name).filename();
  if (name.empty() || name == "." || name == "..") {
    name = fs::u8path("download-" + std::to_string(download_id));
  }
  const fs::path directory = fs::u8path(GetDownloadsDirectory());
  fs::path candidate = directory / name;
  std::error_code error;
  if (!fs::exists(candidate, error)) return PathToUtf8(candidate);

  const std::string stem = PathToUtf8(name.stem());
  const std::string extension = PathToUtf8(name.extension());
  for (unsigned int suffix = 1; suffix < 10000; ++suffix) {
    const fs::path variant = fs::u8path(stem + " (" + std::to_string(suffix) + ")" +
                                         extension);
    candidate = directory / variant;
    error.clear();
    if (!fs::exists(candidate, error)) return PathToUtf8(candidate);
  }
  candidate = directory / ("download-" + std::to_string(download_id));
  return PathToUtf8(candidate);
}

}  // namespace shelter
