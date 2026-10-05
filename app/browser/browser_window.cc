#include "app/browser/browser_window.h"

#include "app/cef/cef_client.h"
#include "app/common/url_utils.h"
#include "include/cef_app.h"
#include "include/cef_frame.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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
    window->SetToFillLayout();
    window->AddChildView(ui_view_);
    window->SetBounds(CefRect(100, 80, 1280, 820));
    window->Show();
    ui_view_->RequestFocus();
  }

  bool CanClose(CefRefPtr<CefWindow>) override { return true; }

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
      nullptr, nullptr, nullptr);
  if (!ui_view_) {
    CefQuitMessageLoop();
    return;
  }

  window_ = CefWindow::CreateTopLevelWindow(
      new ShellWindowDelegate(this, ui_view_));
  if (!window_) CefQuitMessageLoop();
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
  ++live_browsers_;
  if (window_destroyed_ || !window_) {
    if (browser) browser->GetHost()->CloseBrowser(true);
    return;
  }
  auto it = web_tabs_.find(tab_id);
  if (it == web_tabs_.end()) {
    if (browser) browser->GetHost()->CloseBrowser(true);
    return;
  }
  if (it->second.closing) {
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  if (!it->second.pending_url.empty()) {
    const std::string pending_url = std::move(it->second.pending_url);
    it->second.pending_url.clear();
    controller_.Navigate(tab_id, pending_url);
  }
  UpdateWebTabBoundsAndVisibility();
}

void BrowserWindow::OnWebBrowserClosed(const std::string& tab_id,
                                       CefRefPtr<CefBrowser>) {
  CEF_REQUIRE_UI_THREAD();
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
  DispatchToUi("title", data);
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
  DispatchToUi("loading", data);
}

void BrowserWindow::OnWebFullscreenChanged(CefRefPtr<CefBrowser> browser,
                                           bool fullscreen) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !window_->IsValid()) return;
  window_->SetFullscreen(fullscreen);
  auto data = NewDictionary();
  data->SetBool("fullscreen", fullscreen);
  data->SetBool("maximized", window_->IsMaximized());
  DispatchToUi("window-state", data);
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
  DispatchToUi("download-prompt", data);
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
  DispatchToUi("download", data);

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
    if (tab_id.empty() || !IsWebUrl(url) || !EnsureWebTab(tab_id, url)) return false;
    active_tab_id_ = tab_id;
    content_visible_ = true;
    UpdateWebTabBoundsAndVisibility();
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

  CefBrowserSettings settings;
  auto view = CefBrowserView::CreateBrowserView(
      new Client(this, &controller_, tab_id, false), url, settings,
      nullptr, nullptr, nullptr);
  if (!view) {
    controller_.tabs().Close(tab_id);
    return false;
  }
  auto overlay = window_->AddOverlayView(view, CEF_DOCKING_MODE_CUSTOM, true);
  if (!overlay) {
    // This detached view has not created a browser and is released here.
    controller_.tabs().Close(tab_id);
    return false;
  }

  WebTab web_tab;
  web_tab.view = view;
  web_tab.overlay = overlay;
  web_tab.requested_url = url;
  web_tabs_.emplace(tab_id, std::move(web_tab));
  overlay->SetBounds(viewport_bounds_);
  overlay->SetVisible(false);
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

void BrowserWindow::DispatchWebTabState(const std::string& tab_id) {
  auto tab = controller_.tabs().Find(tab_id);
  if (!tab) return;
  auto data = NewDictionary();
  data->SetString("tabId", tab_id);
  data->SetString("url", tab->url);
  data->SetBool("canGoBack", tab->browser && tab->browser->CanGoBack());
  data->SetBool("canGoForward", tab->browser && tab->browser->CanGoForward());
  DispatchToUi("url", data);
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
