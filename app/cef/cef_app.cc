#include "app/cef/cef_app.h"

#include <cmath>
#include <cstdlib>
#include <string>

#include "app/cef/bridge.h"
#include "app/cef/cef_client.h"
#include "app/cef/ui_scheme.h"
#include "app/common/logging.h"
#include "app/common/url_utils.h"
#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_helpers.h"

namespace shelter {
namespace {
constexpr int kInitialWidth = 1440;
constexpr int kInitialHeight = 900;
constexpr int kDevToolsWidth = 1100;
constexpr int kDevToolsHeight = 760;

// Main SHELTER window: hosts the UI browser (full client area) plus content
// browser views positioned by viewport:sync.
class ShellWindowDelegate final : public CefWindowDelegate {
 public:
  explicit ShellWindowDelegate(App* app) : app_(app) {}
  ShellWindowDelegate(const ShellWindowDelegate&) = delete;
  ShellWindowDelegate& operator=(const ShellWindowDelegate&) = delete;
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    app_->OnWindowCreated(window);
  }
  bool CanClose(CefRefPtr<CefWindow> window) override {
    return app_->RequestWindowClose();
  }
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override {
    app_->Relayout();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    app_->OnWindowDestroyed();
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
 private:
  App* app_;
  IMPLEMENT_REFCOUNTING(ShellWindowDelegate);
};

// Top-level window for the DevTools browser view. The UI supplies the target
// rect (#dtBody in client coordinates); we translate it to screen bounds.
class DevToolsWindowDelegate final : public CefWindowDelegate {
 public:
  DevToolsWindowDelegate(App* app,
                         CefRefPtr<CefBrowserView> view,
                         CefRect bounds,
                         bool has_bounds)
      : app_(app), view_(view), bounds_(bounds), has_bounds_(has_bounds) {}
  DevToolsWindowDelegate(const DevToolsWindowDelegate&) = delete;
  DevToolsWindowDelegate& operator=(const DevToolsWindowDelegate&) = delete;
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->SetTitle("DevTools — SHELTER");
    if (view_) window->AddChildView(view_);
    if (!has_bounds_) window->CenterWindow(CefSize(kDevToolsWidth, kDevToolsHeight));
    window->Show();
    if (view_) view_->RequestFocus();
  }
  CefRect GetInitialBounds(CefRefPtr<CefWindow> window) override {
    return has_bounds_ ? bounds_ : CefRect();
  }
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    return CefSize(kDevToolsWidth, kDevToolsHeight);
  }
  bool CanClose(CefRefPtr<CefWindow> window) override {
    CefRefPtr<CefBrowser> browser = view_ ? view_->GetBrowser() : nullptr;
    if (browser) return browser->GetHost()->TryCloseBrowser();
    return true;
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    view_ = nullptr;
    app_ = nullptr;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
 private:
  App* app_;
  CefRefPtr<CefBrowserView> view_;
  const CefRect bounds_;
  const bool has_bounds_;
  IMPLEMENT_REFCOUNTING(DevToolsWindowDelegate);
};

// Browser view delegate. Hosts DevTools popups in their own window; regular
// popups are cancelled in Client::OnBeforePopup so they never reach here.
class ShellBrowserViewDelegate final : public CefBrowserViewDelegate {
 public:
  explicit ShellBrowserViewDelegate(App* app) : app_(app) {}
  ShellBrowserViewDelegate(const ShellBrowserViewDelegate&) = delete;
  ShellBrowserViewDelegate& operator=(const ShellBrowserViewDelegate&) = delete;
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override {
    const bool place = is_devtools && app_->has_devtools_window_bounds();
    CefWindow::CreateTopLevelWindow(new DevToolsWindowDelegate(
        app_, popup_browser_view, app_->devtools_window_bounds(), place));
    return true;
  }
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
 private:
  App* app_;
  IMPLEMENT_REFCOUNTING(ShellBrowserViewDelegate);
};
}  // namespace

App::App() = default;

void App::OnBeforeCommandLineProcessing(const CefString& t,
                                        CefRefPtr<CefCommandLine> c) {
  if (t.empty()) {
#if defined(__APPLE__) || defined(OS_MAC)
    // Avoid real Keychain access (blocks or prompts on unsigned/CI runs;
    // this also matches cefclient and the reference shell).
    c->AppendSwitch("use-mock-keychain");
#endif
    c->AppendSwitch("disable-background-networking");
    c->AppendSwitch("no-default-browser-check");
    c->AppendSwitch("disable-sync");
    c->AppendSwitch("no-first-run");
    c->AppendSwitchWithValue("disable-features", "Translate");
    // CI diagnostics: surface Chromium's own navigation/renderer-launch logs.
    c->AppendSwitch("enable-logging");
    c->AppendSwitchWithValue("v", "1");
    c->AppendSwitchWithValue(
        "vmodule", "navigation_request=1,render_process_host_impl=1,"
                   "child_process_launcher*=1,render_frame_host_manager=1,"
                   "intercept_navigation_throttle=1,throttle_handler=1,"
                   "browser_info_manager=1");
  }
}

void App::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> r) {
  r->AddCustomScheme("shelter",
                     CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                         CEF_SCHEME_OPTION_CORS_ENABLED |
                         CEF_SCHEME_OPTION_FETCH_ENABLED);
}

// ---------------------------------------------------------------------------
// Renderer process: message router side (window.cefQuery for the UI page).
// ---------------------------------------------------------------------------
void App::OnContextCreated(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) {
  if (!renderer_router_) {
    renderer_router_ =
        CefMessageRouterRendererSide::Create(CefMessageRouterConfig());
  }
  renderer_router_->OnContextCreated(browser, frame, context);
}

void App::OnContextReleased(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefV8Context> context) {
  if (renderer_router_) {
    renderer_router_->OnContextReleased(browser, frame, context);
  }
}

bool App::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefProcessId source_process,
                                   CefRefPtr<CefProcessMessage> message) {
  return renderer_router_ &&
         renderer_router_->OnProcessMessageReceived(browser, frame,
                                                    source_process, message);
}

// ---------------------------------------------------------------------------
// Browser process: shell setup.
// ---------------------------------------------------------------------------
void App::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  Log(LogLevel::Info, "CEF context initialized");
  RegisterUiScheme();

  view_delegate_ = new ShellBrowserViewDelegate(this);
  CefBrowserSettings settings;
  CefRefPtr<Client> ui_client(new Client(this, Client::Role::kUi, std::string()));
  // CI diagnostic: SHELTER_SMOKE_URL overrides the UI URL to isolate
  // renderer/navigation machinery from the shelter:// scheme path.
  const char* smoke_url = std::getenv("SHELTER_SMOKE_URL");
  const std::string ui_url =
      (smoke_url && *smoke_url) ? smoke_url : "shelter://ui/index.html";
  Log(LogLevel::Info, "shell: creating ui browser view url=" + ui_url);
  ui_view_ = CefBrowserView::CreateBrowserView(
      ui_client, ui_url, settings, nullptr, nullptr,
      view_delegate_);
  Log(LogLevel::Info, "shell: ui browser view created");
  CefWindow::CreateTopLevelWindow(new ShellWindowDelegate(this));
  Log(LogLevel::Info, "shell: top level window create returned");
}

void App::OnWindowCreated(CefRefPtr<CefWindow> window) {
  Log(LogLevel::Info, "shell: window created");
  window_ = window;
  window->SetTitle("SHELTER");
  window->CenterWindow(CefSize(kInitialWidth, kInitialHeight));
  if (ui_view_) window->AddChildView(ui_view_);
  window->Show();
  Log(LogLevel::Info, "shell: window shown");
  Relayout();
  if (ui_view_) ui_view_->RequestFocus();
  // The initial URL passed to CreateBrowserView does not always start a
  // navigation for views-hosted Alloy browsers; if the main frame is still
  // empty after the window is shown, issue the UI navigation explicitly.
  if (ui_browser_) {
    CefRefPtr<CefFrame> main = ui_browser_->GetMainFrame();
    const std::string url = main ? main->GetURL().ToString() : "<no-frame>";
    Log(LogLevel::Info, "shell: ui main frame url=" + url);
    if (main && main->GetURL().empty()) {
      Log(LogLevel::Info, "shell: ui main frame empty (provisional load)");
    }
  } else {
    Log(LogLevel::Info, "shell: ui browser not ready at window shown");
  }
}

void App::Relayout() {
  if (!window_) return;
  const CefRect client = window_->GetClientAreaBoundsInScreen();
  if (ui_view_ && client.width > 0 && client.height > 0) {
    ui_view_->SetBounds(CefRect(0, 0, client.width, client.height));
  }
  if (!shown_tab_.empty() && viewport_rect_.width > 0 &&
      viewport_rect_.height > 0) {
    auto it = content_.find(shown_tab_);
    if (it != content_.end() && it->second) {
      it->second->SetBounds(viewport_rect_);
    }
  }
  // Notify the UI about window state changes (native maximize/restore).
  const int maximized = window_->IsMaximized() ? 1 : 0;
  if (maximized != last_maximized_) {
    last_maximized_ = maximized;
    DispatchToUi("window-state",
                 std::string("{\"maximized\":") +
                     (maximized ? "true" : "false") + "}");
  }
}

bool App::RequestWindowClose() {
  if (!closing_) {
    closing_ = true;
    // Close content browsers first; the UI browser must stay alive until the
    // end so bridge events keep flowing during shutdown.
    for (auto& entry : content_) {
      if (entry.second && entry.second->GetBrowser()) {
        entry.second->GetBrowser()->GetHost()->CloseBrowser(true);
      }
    }
    if (content_.empty()) return StartUiClose();
    return false;  // window close continues after content browsers are gone
  }
  if (!ui_close_started_) return StartUiClose();
  return true;
}

bool App::StartUiClose() {
  if (ui_close_started_) return true;
  ui_close_started_ = true;
  if (ui_browser_) return ui_browser_->GetHost()->TryCloseBrowser();
  return true;
}

void App::OnWindowDestroyed() {
  window_ = nullptr;
  ui_view_ = nullptr;
  // Safety net for close paths that bypass RequestWindowClose.
  for (auto& entry : content_) {
    if (entry.second && entry.second->GetBrowser()) {
      entry.second->GetBrowser()->GetHost()->CloseBrowser(true);
    }
  }
  if (ui_browser_ && !ui_close_started_) {
    ui_close_started_ = true;
    ui_browser_->GetHost()->CloseBrowser(true);
  }
}

void App::OnUiBrowserCreated(CefRefPtr<CefBrowser> browser) {
  Log(LogLevel::Info, "shell: ui browser OnAfterCreated");
  ui_browser_ = browser;
  if (!ui_view_) ui_view_ = CefBrowserView::GetForBrowser(browser);
}

void App::OnUiBrowserClosed() {
  ui_browser_ = nullptr;
  MaybeQuit();
}

void App::OnContentBrowserCreated(const std::string& tab_id,
                                  CefRefPtr<CefBrowser> browser) {
  // Window close in progress: never let a freshly created browser linger.
  if (closing_) {
    pending_urls_.erase(tab_id);
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  // Apply a navigation that arrived while the browser was being created.
  auto pending = pending_urls_.find(tab_id);
  if (pending != pending_urls_.end()) {
    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (frame && frame->GetURL().ToString() != pending->second) {
      frame->LoadURL(pending->second);
    }
    pending_urls_.erase(pending);
  }
  if (zoom_level_ != 0.0) browser->GetHost()->SetZoomLevel(zoom_level_);
}

void App::OnContentBrowserClosed(const std::string& tab_id) {
  content_.erase(tab_id);
  pending_urls_.erase(tab_id);
  if (tab_id == shown_tab_) shown_tab_.clear();
  if (closing_ && content_.empty() && !ui_close_started_) StartUiClose();
  MaybeQuit();
}

void App::MaybeQuit() {
  if (!ui_browser_ && content_.empty()) CefQuitMessageLoop();
}

// ---------------------------------------------------------------------------
// Content events -> UI page.
// ---------------------------------------------------------------------------
void App::OnContentTitle(const std::string& tab_id,
                         const std::string& title) {
  DispatchToUi("title",
               "{\"tabId\":" + JsonQuote(tab_id) +
                   ",\"title\":" + JsonQuote(title) + "}");
}

void App::OnContentAddress(const std::string& tab_id,
                           const std::string& url) {
  controller_.SetAddress(tab_id, url);
  // Keep the UI history stack in sync: this handles link clicks (new entry),
  // redirects (neighbor entries) and mouse back/forward inside content pages.
  if (ui_browser_ && ui_browser_->GetMainFrame()) {
    ui_browser_->GetMainFrame()->ExecuteJavaScript(
        "window.pushTabHist&&window.pushTabHist(" + JsonQuote(url) + "," +
            JsonQuote(tab_id) + ");",
        "shelter://ui/bridge", 0);
  }
}

void App::OnContentLoading(const std::string& tab_id,
                           bool loading,
                           bool can_back,
                           bool can_forward) {
  DispatchToUi("loading",
               "{\"tabId\":" + JsonQuote(tab_id) +
                   ",\"loading\":" + (loading ? "true" : "false") + "}");
  if (tab_id == active_tab_ && ui_browser_ && ui_browser_->GetMainFrame()) {
    ui_browser_->GetMainFrame()->ExecuteJavaScript(
        std::string("window.onNativeLoading&&window.onNativeLoading(") +
            (loading ? "true" : "false") + ");",
        "shelter://ui/bridge", 0);
  }
}

void App::OnContentLoadStart(const std::string& tab_id,
                             cef_transition_type_t transition) {
  // Record link clicks in the UI history. The UI records its own omnibox
  // navigations inside loadUrl(); redirects are excluded to avoid duplicates.
  if ((transition & TT_SOURCE_MASK) != TT_LINK) return;
  if ((transition & TT_IS_REDIRECT_MASK) != 0) return;
  TabState* tab = controller_.tabs().Find(tab_id);
  if (!tab || tab->url.empty()) return;
  DispatchToUi("visit",
               "{\"url\":" + JsonQuote(tab->url) + ",\"title\":\"\"}");
}

// ---------------------------------------------------------------------------
// Bridge API.
// ---------------------------------------------------------------------------
bool App::NavigateContent(const std::string& tab_id, const std::string& url,
                          bool reveal) {
  CEF_REQUIRE_UI_THREAD();
  if (tab_id.empty() || !IsValidNavigationUrl(url)) return false;
  TabState* tab = controller_.tabs().Find(tab_id);
  if (!tab) tab = controller_.tabs().Create(tab_id, url);
  if (!tab) return false;
  pending_urls_[tab_id] = url;

  auto it = content_.find(tab_id);
  if (it == content_.end() || !it->second) {
    if (!window_) return false;
    CefBrowserSettings settings;
    CefRefPtr<Client> client(
        new Client(this, Client::Role::kContent, tab_id));
    CefRefPtr<CefBrowserView> view = CefBrowserView::CreateBrowserView(
        client, url, settings, nullptr, nullptr, view_delegate_);
    content_[tab_id] = view;
    // Added last so content views paint above the UI browser.
    window_->AddChildView(view);
    view->SetBounds(viewport_rect_);
    const bool show = content_visible_ && active_tab_ == tab_id &&
                      viewport_rect_.width > 0 && viewport_rect_.height > 0;
    view->SetVisible(show);
    if (show) {
      view->RequestFocus();
      shown_tab_ = tab_id;
    }
    return true;
  }

  CefRefPtr<CefBrowserView> view = it->second;
  CefRefPtr<CefBrowser> browser = view->GetBrowser();
  if (!browser) {
    return true;  // creation in flight; pending URL applied in OnAfterCreated
  }
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  const std::string current =
      frame ? frame->GetURL().ToString() : std::string();
  if (current != url) {
    controller_.Navigate(tab_id, url);
  } else {
    controller_.SetAddress(tab_id, url);
  }
  if (reveal) ShowContentTab(tab_id);
  return true;
}

void App::ShowContentTab(const std::string& tab_id) {
  auto it = content_.find(tab_id);
  if (it == content_.end() || !it->second) return;
  CefRefPtr<CefBrowserView> view = it->second;
  if (viewport_rect_.width > 0 && viewport_rect_.height > 0) {
    view->SetBounds(viewport_rect_);
  }
  view->SetVisible(true);
  view->RequestFocus();
  shown_tab_ = tab_id;
}

void App::HideAllContent() {
  for (auto& entry : content_) {
    if (entry.second) entry.second->SetVisible(false);
  }
}

void App::HideContent() {
  content_visible_ = false;
  const bool had_shown = !shown_tab_.empty();
  HideAllContent();
  shown_tab_.clear();
  if (had_shown && ui_view_) ui_view_->RequestFocus();
}

void App::SyncViewport(const CefRect& rect, bool visible,
                       const std::string& active_tab) {
  CEF_REQUIRE_UI_THREAD();
  viewport_rect_ = rect;
  active_tab_ = active_tab;
  content_visible_ = visible;

  const std::string want =
      (visible && !active_tab.empty() && rect.width > 0 && rect.height > 0)
          ? active_tab
          : std::string();
  if (want == shown_tab_) {
    if (!want.empty()) {
      auto it = content_.find(want);
      if (it != content_.end() && it->second) it->second->SetBounds(rect);
    }
    return;
  }
  if (!shown_tab_.empty()) {
    auto it = content_.find(shown_tab_);
    if (it != content_.end() && it->second) it->second->SetVisible(false);
  }
  shown_tab_.clear();
  if (want.empty()) {
    // Content hidden (internal page or overlay): hand focus back to the UI.
    if (ui_view_) ui_view_->RequestFocus();
    return;
  }
  if (content_.count(want)) {
    ShowContentTab(want);
  }
}

bool App::StopTab(const std::string& tab_id) {
  return controller_.Stop(tab_id);
}

bool App::ReloadTab(const std::string& tab_id) {
  const std::string id = tab_id.empty() ? active_tab_ : tab_id;
  TabState* tab = controller_.tabs().Find(id);
  if (!tab) return false;
  if (tab->browser) {
    tab->browser->Reload();
    return true;
  }
  if (IsValidNavigationUrl(tab->url)) {
    return NavigateContent(id, tab->url, true);
  }
  return false;
}

void App::ApplyZoom(double factor) {
  if (factor <= 0) return;
  zoom_level_ = std::log(factor) / std::log(1.2);
  for (auto& entry : content_) {
    if (entry.second && entry.second->GetBrowser()) {
      entry.second->GetBrowser()->GetHost()->SetZoomLevel(zoom_level_);
    }
  }
}

bool App::WindowOp(const std::string& op) {
  if (!window_) return false;
  if (op == "minimize") {
    window_->Minimize();
    return true;
  }
  if (op == "maximize") {
    const bool will_maximize = !window_->IsMaximized();
    if (will_maximize) {
      window_->Maximize();
    } else {
      window_->Restore();
    }
    last_maximized_ = will_maximize ? 1 : 0;
    DispatchToUi("window-state",
                 std::string("{\"maximized\":") +
                     (will_maximize ? "true" : "false") + "}");
    return true;
  }
  if (op == "close") {
    window_->Close();
    return true;
  }
  return false;
}

bool App::OpenDevTools(const std::string& tab_id, const CefRect& bounds,
                       bool has_bounds) {
  CEF_REQUIRE_UI_THREAD();
  const std::string id = tab_id.empty() ? active_tab_ : tab_id;
  CefRefPtr<CefBrowser> target;
  auto it = content_.find(id);
  if (it != content_.end() && it->second) target = it->second->GetBrowser();
  if (!target && !shown_tab_.empty()) {
    auto it2 = content_.find(shown_tab_);
    if (it2 != content_.end() && it2->second) target = it2->second->GetBrowser();
  }
  if (!target) return false;

  has_devtools_bounds_ = false;
  if (has_bounds && window_) {
    // The UI rect is relative to the client area (the UI page starts at 0,0).
    const CefRect client = window_->GetClientAreaBoundsInScreen();
    devtools_bounds_ = CefRect(client.x + bounds.x, client.y + bounds.y,
                               bounds.width, bounds.height);
    has_devtools_bounds_ = true;
  }
  CefWindowInfo window_info;
  CefBrowserSettings settings;
  // An empty CefPoint (0,0) means "do not inspect an element".
  target->GetHost()->ShowDevTools(window_info, nullptr, settings, CefPoint());
  return true;
}

void App::CloseDevTools(const std::string& tab_id) {
  const std::string id = tab_id.empty() ? active_tab_ : tab_id;
  auto it = content_.find(id);
  if (it != content_.end() && it->second && it->second->GetBrowser()) {
    it->second->GetBrowser()->GetHost()->CloseDevTools();
  }
}

void App::DispatchToUi(const char* event, const std::string& json_data) {
  if (!ui_browser_ || !ui_browser_->GetMainFrame()) return;
  const std::string code = "window.shelterCefDispatch&&window.shelterCefDispatch(" +
                           JsonQuote(event) + "," + json_data + ");";
  ui_browser_->GetMainFrame()->ExecuteJavaScript(code, "shelter://ui/bridge", 0);
}

}  // namespace shelter
