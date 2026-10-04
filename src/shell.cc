#include "src/shell.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if !defined(OS_WIN)
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include <cctype>
#include <cstring>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_extension.h"
#include "include/cef_extension_handler.h"
#include "include/cef_parser.h"
#include "include/cef_stream.h"
#include "include/cef_urlrequest.h"
#include "include/cef_zip_reader.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_display.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "src/clients.h"
#include "src/common.h"
#include "src/key_map.h"
#include "src/platform.h"
#include "src/ui_scheme.h"

namespace fs = std::filesystem;

namespace shelter {

namespace {

constexpr int kMinWidth = 960;
constexpr int kMinHeight = 620;

CefRefPtr<CefImage> LoadWindowIcon() {
  std::ifstream f(platform::UiResourceDir() + "/icon-256.png", std::ios::binary);
  if (!f) return nullptr;
  std::ostringstream ss;
  ss << f.rdbuf();
  const std::string data = ss.str();
  CefRefPtr<CefImage> img = CefImage::CreateImage();
  if (!img->AddPNG(1.0f, data.data(), data.size())) return nullptr;
  return img;
}

// ---- окно ------------------------------------------------------------------

class ShellWindowDelegate : public CefWindowDelegate {
 public:
  explicit ShellWindowDelegate(CefRefPtr<CefBrowserView> ui_view)
      : ui_view_(ui_view) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    Shell::Get().OnWindowCreated(window);
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    ui_view_ = nullptr;
    Shell::Get().OnWindowDestroyed();
  }
  bool CanClose(CefRefPtr<CefWindow> window) override {
    return Shell::Get().CanCloseWindow();
  }

  // Окно без системной рамки: заголовок, кнопки и перетаскивание — в вёрстке.
  bool IsFrameless(CefRefPtr<CefWindow>) override { return true; }
  // macOS: оставляем нативные «светофоры» поверх вёрстки.
  bool WithStandardWindowButtons(CefRefPtr<CefWindow>) override { return true; }
  bool GetTitlebarHeight(CefRefPtr<CefWindow>, float* height) override {
    *height = 48.f;  // центр кнопок по вертикали = 24 px (как .sb-top в вёрстке)
    return true;
  }
  bool CanResize(CefRefPtr<CefWindow>) override { return true; }
  bool CanMaximize(CefRefPtr<CefWindow>) override { return true; }
  bool CanMinimize(CefRefPtr<CefWindow>) override { return true; }

  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    int w = 1440, h = 900;
    if (auto d = CefDisplay::GetPrimaryDisplay()) {
      CefRect wa = d->GetWorkArea();
      w = std::min(w, std::max(kMinWidth, wa.width - 80));
      h = std::min(h, std::max(kMinHeight, wa.height - 80));
    }
    return CefSize(w, h);
  }
  CefSize GetMinimumSize(CefRefPtr<CefView>) override {
    return CefSize(kMinWidth, kMinHeight);
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  CefRefPtr<CefBrowserView> ui_view_;
  IMPLEMENT_REFCOUNTING(ShellWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(ShellWindowDelegate);
};

// Окно для popup'ов страниц и DevTools.
class PopupWindowDelegate : public CefWindowDelegate {
 public:
  explicit PopupWindowDelegate(CefRefPtr<CefBrowserView> view) : view_(view) {}

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    window->AddChildView(view_);
    window->Show();
    Shell::Get().TrackPopupWindow(window);
    view_->RequestFocus();
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    Shell::Get().UntrackPopupWindow(window);
    view_ = nullptr;
  }
  bool CanClose(CefRefPtr<CefWindow>) override {
    CefRefPtr<CefBrowser> b = view_ ? view_->GetBrowser() : nullptr;
    return b ? b->GetHost()->TryCloseBrowser() : true;
  }
  CefSize GetPreferredSize(CefRefPtr<CefView>) override {
    return CefSize(1000, 720);
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  CefRefPtr<CefBrowserView> view_;
  IMPLEMENT_REFCOUNTING(PopupWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(PopupWindowDelegate);
};

// ---- расширения: CRX -> распаковка -> CefRequestContext::LoadExtension ----

std::string ExtManifestString(CefRefPtr<CefExtension> e, const char* key) {
  if (!e) return std::string();
  CefRefPtr<CefDictionaryValue> m = e->GetManifest();
  if (m && m->HasKey(key) && m->GetType(key) == VTYPE_STRING)
    return m->GetString(key).ToString();
  return std::string();
}

std::string BuildExtListJson() {
  CefRefPtr<CefRequestContext> ctx = CefRequestContext::GetGlobalContext();
  std::ostringstream os;
  os << "{\"list\":[";
  if (ctx) {
    std::vector<CefString> ids;
    ctx->GetExtensions(ids);
    bool first = true;
    for (const auto& id : ids) {
      CefRefPtr<CefExtension> e = ctx->GetExtension(id);
      if (!e) continue;
      std::string name = ExtManifestString(e, "name");
      if (name.empty()) name = id.ToString();
      if (!first) os << ",";
      first = false;
      os << "{\"id\":" << JsString(id.ToString())
         << ",\"name\":" << JsString(name)
         << ",\"ver\":" << JsString(ExtManifestString(e, "version"))
         << ",\"path\":" << JsString(e->GetPath().ToString()) << "}";
    }
  }
  os << "]}";
  return os.str();
}

class ShellExtHandler : public CefExtensionHandler {
 public:
  ShellExtHandler() = default;
  void OnExtensionLoadFailed(cef_errorcode_t err) override {
    Shell::Get().ExtToast(
        "Расширение не загрузилось (несовместимо с Shelter, код " +
            std::to_string(static_cast<int>(err)) + ")",
        true);
  }
  void OnExtensionLoaded(CefRefPtr<CefRequestContext>,
                         CefRefPtr<CefExtension> ext) override {
    const std::string name = ExtManifestString(ext, "name");
    Shell::Get().ExtToast(name.empty() ? "Расширение загружено"
                                       : "Расширение загружено: " + name,
                          false);
    Shell::Get().ExtPushList();
  }
  void OnExtensionUnloaded(CefRefPtr<CefRequestContext>,
                           CefRefPtr<CefExtension>) override {
    Shell::Get().ExtPushList();
  }

 private:
  IMPLEMENT_REFCOUNTING(ShellExtHandler);
  DISALLOW_COPY_AND_ASSIGN(ShellExtHandler);
};

// Загрузка .crx/.zip пакета расширения через CefURLRequest.
class CrxDownload : public CefURLRequestClient {
 public:
  explicit CrxDownload(std::string origin) : origin_(std::move(origin)) {}
  void Start(const std::string& url) {
    CefRefPtr<CefRequest> req = CefRequest::Create();
    req->SetURL(url);
    req->SetMethod("GET");
    CefURLRequest::Create(req, this, CefRequestContext::GetGlobalContext());
  }

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    if (request->GetRequestStatus() == UR_SUCCESS && !buf_.empty()) {
      Shell::Get().ExtInstallBytes(std::move(buf_), origin_);
    } else {
      Shell::Get().ExtToast(
          "Не удалось скачать пакет расширения (сеть или магазин недоступны)",
          true);
    }
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnResponseReceived(CefRefPtr<CefURLRequest>,
                          CefRefPtr<CefResponse>) override {}
  void OnReadResponse(CefRefPtr<CefURLRequest>, void* data_out,
                      size_t bytes_to_read, int64_t& bytes_read, void*& buffer,
                      size_t& buffer_size) override {
    const size_t have = buf_.size() - pos_;
    const size_t n = std::min(bytes_to_read, have);
    if (n) memcpy(data_out, buf_.data() + pos_, n);
    pos_ += n;
    bytes_read = static_cast<int64_t>(n);
    buffer = nullptr;
    buffer_size = 0;
  }

 private:
  std::string origin_;
  std::string buf_;
  size_t pos_ = 0;
  IMPLEMENT_REFCOUNTING(CrxDownload);
  DISALLOW_COPY_AND_ASSIGN(CrxDownload);
};

uint32_t ReadLe32(const std::string& b, size_t off) {
  uint32_t v = 0;
  memcpy(&v, b.data() + off, 4);
  return v;
}

// Смещение ZIP-части внутри CRX (v2/v3). Для обычного ZIP возвращает 0.
size_t CrxZipOffset(const std::string& b) {
  if (b.size() < 4 || memcmp(b.data(), "Cr24", 4) != 0) return 0;
  if (b.size() < 12) return b.size();
  const uint32_t ver = ReadLe32(b, 4);
  if (ver == 3) {
    const size_t off = 12 + ReadLe32(b, 8);
    return off <= b.size() ? off : b.size();
  }
  if (ver == 2 && b.size() >= 16) {
    const size_t off = 16 + ReadLe32(b, 8) + ReadLe32(b, 12);
    return off <= b.size() ? off : b.size();
  }
  return b.size();
}

bool SafeZipName(const std::string& name) {
  if (name.empty() || name[0] == '/') return false;
  size_t start = 0;
  while (start <= name.size()) {
    size_t slash = name.find('/', start);
    const std::string part = name.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    if (part == "..") return false;
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return true;
}

bool UnzipBytesTo(const std::string& bytes, const fs::path& dir) {
  if (bytes.empty()) return false;
  CefRefPtr<CefStreamReader> sr = CefStreamReader::CreateForData(
      const_cast<char*>(bytes.data()), bytes.size());
  if (!sr) return false;
  CefRefPtr<CefZipReader> zr = CefZipReader::Create(sr);
  if (!zr || !zr->MoveToFirstFile()) return false;
  bool any = false;
  do {
    std::string name = zr->GetFileName().ToString();
    for (auto& ch : name) {
      if (ch == '\\') ch = '/';
    }
    if (!SafeZipName(name)) continue;
    fs::path fp = dir / name;
    std::error_code ec;
    if (name.back() == '/') {
      fs::create_directories(fp, ec);
      continue;
    }
    fs::create_directories(fp.parent_path(), ec);
    if (zr->OpenFile(CefString(), true)) {
      std::ofstream out(fp, std::ios::binary | std::ios::trunc);
      char chunk[65536];
      size_t n = 0;
      while ((n = zr->ReadFile(chunk, sizeof(chunk))) > 0)
        out.write(chunk, static_cast<std::streamsize>(n));
      out.close();
      zr->CloseFile();
      any = true;
    }
  } while (zr->MoveToNextFile());
  return any;
}

}  // namespace

bool ShellBrowserViewDelegate::OnPopupBrowserViewCreated(
    CefRefPtr<CefBrowserView>, CefRefPtr<CefBrowserView> popup_browser_view,
    bool) {
  CefWindow::CreateTopLevelWindow(new PopupWindowDelegate(popup_browser_view));
  return true;
}

// ============================================================================
// Shell
// ============================================================================

Shell& Shell::Get() {
  static Shell* instance = new Shell();
  return *instance;
}

void Shell::Start() {
  CEF_REQUIRE_UI_THREAD();
  ApplyPendingWipes();
  RegisterUiScheme();
  ui_view_ = CreateUiView();
  CefWindow::CreateTopLevelWindow(new ShellWindowDelegate(ui_view_));
}

CefRefPtr<CefBrowserView> Shell::CreateUiView() {
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 8, 9, 11);  // #08090B — без белой вспышки
  // CI: SHELTER_SMOKE_URL переключает стартовую страницу (например,
  // shelter://app/index.html?smoke=site — UI сам откроет сайт после загрузки).
  std::string url = kUiUrl;
  if (const char* smoke = std::getenv("SHELTER_SMOKE_URL")) {
    if (*smoke) url = smoke;
  }
  return CefBrowserView::CreateBrowserView(new UiClient(), url, settings,
                                           nullptr, nullptr,
                                           new ShellBrowserViewDelegate());
}

void Shell::WindowMiniaturized(void* ctx) {
  Shell* s = static_cast<Shell*>(ctx);
  if (s->window_) {
    s->window_->Hide();
    s->Log("shell: window miniaturized -> hide widget (frames paused)");
  }
}

void Shell::WindowDeminiaturized(void* ctx) {
  Shell* s = static_cast<Shell*>(ctx);
  if (s->window_) {
    s->window_->Show();
    s->Log("shell: window deminiaturized -> show widget");
  }
}

void Shell::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window_ = window;
  window_->SetTitle(std::string(kAppName) + " · " + kAppVersion);
  // Миниатюра: CEF Views не пробрасывает сворачивание в виджет — без Hide/Show
  // рендереры и GPU продолжают кадры для свёрнутого окна (фоновое горение CPU).
  platform::WatchMainWindow(reinterpret_cast<void*>(window_->GetWindowHandle()),
                            &Shell::WindowMiniaturized,
                            &Shell::WindowDeminiaturized, this);
  if (auto icon = LoadWindowIcon()) {
    window_->SetWindowIcon(icon);
    window_->SetWindowAppIcon(icon);
  }
  // AddOverlayView() добавляет в корень окна пустой views::View («z-order reference»).
  // При дефолтном FillLayout он растягивается на всё окно и перехватывает hit-test Views,
  // из‑за чего NSView интерфейса на macOS перестаёт получать мышь. BoxLayout с flex только
  // у UI-вида оставляет таким служебным видам нулевую ширину.
  CefBoxLayoutSettings bl;
  bl.horizontal = true;
  bl.inside_border_insets = CefInsets(0, 0, 0, 0);
  bl.between_child_spacing = 0;
  bl.main_axis_alignment = CEF_AXIS_ALIGNMENT_START;
  bl.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  bl.minimum_cross_axis_size = 0;
  bl.default_flex = 0;
  CefRefPtr<CefBoxLayout> layout = window_->SetToBoxLayout(bl);
  window_->AddChildView(ui_view_);
  if (layout) layout->SetFlexForView(ui_view_, 1);
  window_->Show();
  ui_view_->RequestFocus();
  platform::InstallInputFixes();
}

bool Shell::CanCloseWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (!closing_) {
    closing_ = true;
    std::vector<std::string> ids;
    for (auto& kv : tabs_) ids.push_back(kv.first);
    for (auto& id : ids) DestroyTab(id);
    for (auto& w : std::set<CefRefPtr<CefWindow>>(popup_windows_)) w->Close();
  }
  CefRefPtr<CefBrowser> b = ui_view_ ? ui_view_->GetBrowser() : nullptr;
  return b ? b->GetHost()->TryCloseBrowser() : true;
}

void Shell::OnWindowDestroyed() {
  CEF_REQUIRE_UI_THREAD();
  if (window_) {
    platform::UnwatchMainWindow(reinterpret_cast<void*>(window_->GetWindowHandle()));
  }
  window_ = nullptr;
  ui_view_ = nullptr;
  closing_ = true;
  for (auto& w : std::set<CefRefPtr<CefWindow>>(popup_windows_)) w->Close();
  MaybeQuit();
}

void Shell::RequestClose() {
  CEF_REQUIRE_UI_THREAD();
  if (window_) {
    window_->Close();
  } else {
    closing_ = true;
    MaybeQuit();
  }
}

void Shell::OnBrowserCreated() { ++browser_count_; }

void Shell::OnBrowserClosed() {
  if (browser_count_ > 0) --browser_count_;
  MaybeQuit();
}

// Выходим из message loop, когда окна нет и все браузеры уничтожены.
void Shell::MaybeQuit() {
  if (closing_ && !window_ && browser_count_ == 0 && !quit_posted_) {
    quit_posted_ = true;
    CefQuitMessageLoop();
  }
}

void Shell::TrackPopupWindow(CefRefPtr<CefWindow> window) {
  popup_windows_.insert(window);
}
void Shell::UntrackPopupWindow(CefRefPtr<CefWindow> window) {
  popup_windows_.erase(window);
}

// ---- UI-браузер ------------------------------------------------------------

void Shell::OnUiCreated(CefRefPtr<CefBrowser> browser) { ui_browser_ = browser; }

void Shell::OnUiClosed(CefRefPtr<CefBrowser>) { ui_browser_ = nullptr; }

bool Shell::IsUiBrowser(CefRefPtr<CefBrowser> browser) const {
  return browser && ui_browser_ &&
         browser->GetIdentifier() == ui_browser_->GetIdentifier();
}

void Shell::SetDraggableRegions(const std::vector<CefDraggableRegion>& regions) {
  drag_regions_ = regions;
  if (window_) window_->SetDraggableRegions(regions);
}

void Shell::UiEvent(const std::string& name, const std::string& payload_json) {
  if (!ui_browser_) return;
  CefRefPtr<CefFrame> frame = ui_browser_->GetMainFrame();
  if (!frame || !frame->IsValid()) return;
  const std::string js = "window.__shelterHost&&window.__shelterHost.ev(" +
                         JsString(name) + "," + payload_json + ");";
  frame->ExecuteJavaScript(js, kUiUrl, 0);
}

// ---- контексты (сессии) ----------------------------------------------------

CefRefPtr<CefRequestContext> Shell::ContextFor(const std::string& partition) {
  auto it = contexts_.find(partition);
  if (it != contexts_.end()) return it->second;

  CefRequestContextSettings rs;
  if (partition.rfind("persist:", 0) == 0) {
    const std::string dir = platform::UserDataDir() + "/Profiles/" +
                            SanitizeForPath(partition.substr(8));
    CefString(&rs.cache_path) = dir;
    rs.persist_session_cookies = true;
  }
  // temp:* и прочее — cache_path пуст: контекст только в памяти (без следов на диске).
  CefRefPtr<CefRequestContext> ctx = CefRequestContext::CreateContext(rs, nullptr);
  contexts_[partition] = ctx;
  return ctx;
}

void Shell::ReleaseContextIfUnused(const std::string& partition) {
  if (partition.rfind("temp:", 0) != 0) return;  // persist-контексты держим
  for (auto& kv : tabs_) {
    if (kv.second.partition == partition) return;
  }
  contexts_.erase(partition);
}

void Shell::QueueWipe(const std::string& partition) {
  if (partition.rfind("persist:", 0) != 0) return;
  std::error_code ec;
  fs::create_directories(platform::UserDataDir(), ec);
  std::ofstream f(platform::UserDataDir() + "/pending-wipe.txt", std::ios::app);
  f << SanitizeForPath(partition.substr(8)) << "\n";
}

// Профили, помеченные «сжечь», удаляются при следующем старте (пока они не открыты).
// Перед unlink — однопроходное затирание содержимого (best effort):
//  - реально помогает на HDD/внешних флешках и против простого file-carving;
//  - на SSD/APFS физическую гарантию даёт TRIM + FileVault (CoW-журнал и
//    wear-leveling переживают перезапись) — поэтому бюджет затирания ограничен,
//    чтобы не накручивать износ и не тормозить старт: файлы >32 МБ (кэши
//    страниц) только удаляются, чувствительные БД/хранилища (Cookies, History,
//    Local Storage, Logins) всегда мельше порога.
void Shell::ApplyPendingWipes() {
  const std::string list = platform::UserDataDir() + "/pending-wipe.txt";
  std::ifstream f(list);
  if (!f) return;
  std::string line;
  std::error_code ec;
  constexpr std::uintmax_t kMaxOverwriteBytes = 32ull * 1024 * 1024;
  auto overwrite_file = [&](const fs::path& p) {
    std::error_code tec;
    const std::uintmax_t sz = fs::file_size(p, tec);
    if (tec || sz == 0 || sz > kMaxOverwriteBytes) return;
    std::fstream out(p, std::ios::in | std::ios::out | std::ios::binary);
    if (!out) return;
    const std::size_t kChunk = 64 * 1024;
    std::vector<char> zeros(kChunk, 0);
    std::uintmax_t left = sz;
    out.seekp(0);
    while (left > 0 && out) {
      const std::size_t n = static_cast<std::size_t>(left < kChunk ? left : kChunk);
      out.write(zeros.data(), static_cast<std::streamsize>(n));
      left -= n;
    }
    out.flush();
  };
  auto secure_remove = [&](const fs::path& root) {
    std::error_code ec2;
    if (!fs::exists(root, ec2)) return;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec2);
    const fs::recursive_directory_iterator end;
    while (!ec2 && it != end) {
      std::error_code tec;
      if (it->is_regular_file(tec) && !it->is_symlink(tec)) overwrite_file(it->path());
      it.increment(ec2);
    }
    fs::remove_all(root, ec2);
  };
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    secure_remove(fs::path(platform::UserDataDir()) / "Profiles" / line);
  }
  f.close();
  // сам список тоже не оставляем с открытыми именами
  overwrite_file(fs::path(list));
  fs::remove(list, ec);
}

// ---- вкладки ---------------------------------------------------------------

Tab* Shell::FindTab(const std::string& id) {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? nullptr : &it->second;
}

Tab* Shell::FindTabByBrowser(int browser_id) {
  for (auto& kv : tabs_) {
    if (kv.second.browser_id == browser_id) return &kv.second;
  }
  return nullptr;
}

Tab* Shell::CreateTab(const std::string& id, const std::string& partition,
                      const std::string& url, double zoom) {
  Tab tab;
  tab.id = id;
  tab.partition = partition;
  tab.requested_url = url;
  tab.client = new TabClient(id);

  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 255, 255, 255);
  tab.view = CefBrowserView::CreateBrowserView(
      tab.client, url, settings, nullptr, ContextFor(partition),
      new ShellBrowserViewDelegate());
  const bool can_activate =
      !CefCommandLine::GetGlobalCommandLine()->HasSwitch("tab-no-activate");
  tab.overlay = window_->AddOverlayView(tab.view, CEF_DOCKING_MODE_CUSTOM,
                                        can_activate);
  tab.overlay->SetVisible(false);
  zoom_ = zoom;
  tabs_[id] = std::move(tab);
  Tab* t = &tabs_[id];
  if (CefRefPtr<CefBrowser> b = t->view->GetBrowser()) {
    OnTabCreated(b, id);
  }
  return t;
}

void Shell::OnTabCreated(CefRefPtr<CefBrowser> browser, const std::string& id) {
  Tab* t = FindTab(id);
  if (!t || t->browser) return;
  t->browser = browser;
  t->browser_id = browser->GetIdentifier();
  browser->GetHost()->SetZoomLevel(std::log(zoom_) / std::log(1.2));
  if (!t->pending_url.empty()) {
    browser->GetMainFrame()->LoadURL(t->pending_url);
    t->pending_url.clear();
  }
}

void Shell::DestroyTab(const std::string& id) {
  auto it = tabs_.find(id);
  if (it == tabs_.end()) return;
  Tab tab = std::move(it->second);
  tabs_.erase(it);
  if (active_tab_ == id) active_tab_.clear();
  if (tab.snap_registration) tab.snap_registration = nullptr;
  if (tab.overlay && tab.overlay->IsValid()) tab.overlay->Destroy();
  // Последняя ссылка на BrowserView уходит вместе с tab — браузер закроется сам
  // (OnBeforeClose придёт позже, TabClient к этому моменту уже «отвязан»).
  tab.overlay = nullptr;
  tab.view = nullptr;
  tab.browser = nullptr;
  ReleaseContextIfUnused(tab.partition);
}

void Shell::HideAllTabs() {
  for (auto& kv : tabs_) {
    if (kv.second.overlay && kv.second.overlay->IsValid()) {
      kv.second.overlay->SetVisible(false);
    }
  }
}

void Shell::LayoutTab(Tab* tab, const CefRect& rect, bool visible) {
  if (!tab || !tab->overlay || !tab->overlay->IsValid()) return;
  const CefRect prev = last_rect_;
  if (rect.width > 0 && rect.height > 0) {
    tab->overlay->SetBounds(rect);
    last_rect_ = rect;
  }
  tab->overlay->SetVisible(visible && last_rect_.width > 0);
  ApplyClip(tab);
  // Приёмочный маркер CI (tools/check_geometry.py): область контента должна
  // оставаться в hero-зоне (сайдбар+тулбар видны) и не перекрывать всё окно.
  const bool changed =
      last_rect_.x != prev.x || last_rect_.y != prev.y ||
      last_rect_.width != prev.width || last_rect_.height != prev.height;
  if (window_ && last_rect_.width > 0 && changed) {
    const CefRect c = window_->GetClientAreaBoundsInScreen();
    Log("shell: content bounds shown x=" + std::to_string(last_rect_.x) +
        " y=" + std::to_string(last_rect_.y) +
        " w=" + std::to_string(last_rect_.width) +
        " h=" + std::to_string(last_rect_.height) +
        " client_w=" + std::to_string(c.width) +
        " client_h=" + std::to_string(c.height));
  }
}

void Shell::OnUiReady(const std::string& version) {
  if (!window_ || version.empty()) return;
  window_->SetTitle(std::string(kAppName) + " · " + version);
}

void Shell::ApplyClip(Tab* tab) {
  if (!tab || !tab->browser || !tab->overlay || !tab->overlay->IsValid()) return;
  void* h = reinterpret_cast<void*>(tab->browser->GetHost()->GetWindowHandle());
  if (!h || last_rect_.width <= 0 || last_rect_.height <= 0) return;
  platform::ApplyViewClip(h, clip_radii_, clip_holes_, last_rect_.width,
                          last_rect_.height);
}

void Shell::Log(const std::string& line) {
  const fs::path p = fs::path(platform::UserDataDir()) / "shelter.log";
  std::ofstream f(p, std::ios::app);
  if (!f) return;
  f << line << "\n";
  f.close();
#if !defined(OS_WIN)
  // Лог содержит URL загрузок и адреса — держим0600 (не шире владельца).
  ::chmod(p.string().c_str(), 0600);
#endif
}

void Shell::ReapplyZoom(CefRefPtr<CefBrowser> browser) {
  // Масштаб в Chromium хранится «по хосту»: после перехода на другой сайт он сбрасывается.
  if (!browser) return;
  const double level = std::log(zoom_) / std::log(1.2);
  if (std::fabs(browser->GetHost()->GetZoomLevel() - level) > 0.01)
    browser->GetHost()->SetZoomLevel(level);
}

CefRefPtr<CefBrowser> Shell::ActiveBrowser() {
  Tab* t = FindTab(active_tab_);
  return t ? t->browser : nullptr;
}

// ---- события вкладок -------------------------------------------------------

namespace {

std::string NormKey(std::string u) {
  auto strip = [&](const char* p) {
    std::string s(p);
    if (u.rfind(s, 0) == 0) u.erase(0, s.size());
  };
  strip("https://");
  strip("http://");
  strip("www.");
  while (!u.empty() && (u.back() == '/' || u.back() == '#')) u.pop_back();
  std::transform(u.begin(), u.end(), u.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return u;
}

}  // namespace

bool SameUrl(const std::string& a, const std::string& b) {
  return NormKey(a) == NormKey(b);
}

void Shell::OnTabAddress(CefRefPtr<CefBrowser> browser, const std::string& url) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return;
  // Страница ошибки (data:) — не адрес вкладки.
  if (!t->error_url.empty() && url.rfind("data:", 0) == 0) return;
  if (url.rfind("data:text/html", 0) != 0) t->error_url.clear();
  t->current_url = url;
  ReapplyZoom(browser);
  if (SameUrl(url, t->requested_url)) return;
  t->requested_url = url;
  UiEvent("nav", "{\"id\":" + JsString(t->id) + ",\"url\":" + JsString(url) + "}");
}

void Shell::OnTabTitle(CefRefPtr<CefBrowser> browser, const std::string& title) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t || title.rfind("data:", 0) == 0) return;
  UiEvent("title",
          "{\"id\":" + JsString(t->id) + ",\"title\":" + JsString(title) + "}");
}

void Shell::OnTabLoading(CefRefPtr<CefBrowser> browser, bool loading) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return;
  t->loading = loading;
  ReapplyZoom(browser);
  UiEvent("loading", "{\"id\":" + JsString(t->id) + ",\"loading\":" +
                         (loading ? "true" : "false") + "}");
}

void Shell::OnTabNewWindow(CefRefPtr<CefBrowser> browser, const std::string& url) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  UiEvent("newtab", "{\"url\":" + JsString(url) + ",\"from\":" +
                        JsString(t ? t->id : "") + "}");
}

void Shell::OnTabFindResult(CefRefPtr<CefBrowser> browser, int count, int idx,
                            bool final_update) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return;
  // Новый поиск подсвечивает все совпадения, но не выделяет первое — делаем это сами.
  if (t->find_kick && final_update && count > 0) {
    t->find_kick = false;
    if (idx == 0 && !t->find_text.empty()) {
      browser->GetHost()->Find(t->find_text, true, false, true);
      return;
    }
  }
  UiEvent("found", "{\"id\":" + JsString(t->id) + ",\"count\":" +
                       std::to_string(count) + ",\"idx\":" +
                       std::to_string(idx) + "}");
}

bool Shell::OnTabKey(CefRefPtr<CefBrowser> browser, const CefKeyEvent& e) {
  std::string code, key;
  if (!MapKeyEvent(e, &code, &key)) return false;

#if defined(OS_MAC)
  const bool mod = (e.modifiers & EVENTFLAG_COMMAND_DOWN) != 0;
#else
  const bool mod = (e.modifiers & EVENTFLAG_CONTROL_DOWN) != 0;
#endif
  const bool shift = (e.modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
  const bool alt = (e.modifiers & EVENTFLAG_ALT_DOWN) != 0;

  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return false;

  // Нативные действия над страницей
  if (code == "F12" || (mod && shift && code == "KeyI")) {
    TabAction(t->id, "devtools", "");
    return true;
  }
  if (code == "F5" || (mod && !shift && !alt && code == "KeyR")) {
    TabAction(t->id, "reload", "");
    return true;
  }
  if (mod && shift && code == "KeyR") {
    TabAction(t->id, "reloadHard", "");
    return true;
  }
  if (alt && !mod && (code == "ArrowLeft" || code == "ArrowRight")) {
    // история ведётся в UI — отдаём ему
  } else if (!mod) {
    return false;
  }

  // Горячие клавиши, которые понимает UI (см. keydown в index.html).
  static const std::set<std::string> kForward = {
      "KeyK", "KeyL", "KeyF", "KeyP", "KeyT", "KeyW", "KeyY", "KeyD", "KeyO",
      "KeyI", "Comma", "Backslash", "Equal", "Minus", "NumpadAdd",
      "NumpadSubtract", "Digit0", "Digit1", "Digit2", "Digit3", "Digit4",
      "Tab", "ArrowLeft", "ArrowRight"};
  if (!kForward.count(code)) return false;

  std::ostringstream os;
  os << "{\"code\":" << JsString(code) << ",\"key\":" << JsString(key)
     << ",\"ctrl\":" << ((e.modifiers & EVENTFLAG_CONTROL_DOWN) ? "true" : "false")
     << ",\"meta\":" << ((e.modifiers & EVENTFLAG_COMMAND_DOWN) ? "true" : "false")
     << ",\"shift\":" << (shift ? "true" : "false")
     << ",\"alt\":" << (alt ? "true" : "false") << "}";
  UiEvent("key", os.str());
  return true;
}

void Shell::OnTabContextMenu(CefRefPtr<CefBrowser> browser,
                             CefRefPtr<CefFrame>,
                             CefRefPtr<CefContextMenuParams> params) {
  Tab* t = FindTabByBrowser(browser->GetIdentifier());
  if (!t) return;
  last_ctx_tab_ = t->id;

  std::string media = "none";
  switch (params->GetMediaType()) {
    case CM_MEDIATYPE_IMAGE: media = "image"; break;
    case CM_MEDIATYPE_VIDEO: media = "video"; break;
    case CM_MEDIATYPE_AUDIO: media = "audio"; break;
    default: break;
  }
  const int flags = params->GetEditStateFlags();
  std::ostringstream os;
  os << "{\"id\":" << JsString(t->id) << ",\"partition\":"
     << JsString(t->partition) << ",\"x\":" << params->GetXCoord()
     << ",\"y\":" << params->GetYCoord() << ",\"params\":{"
     << "\"linkURL\":" << JsString(params->GetLinkUrl().ToString())
     << ",\"srcURL\":" << JsString(params->GetSourceUrl().ToString())
     << ",\"pageURL\":" << JsString(params->GetPageUrl().ToString())
     << ",\"selectionText\":" << JsString(params->GetSelectionText().ToString())
     << ",\"mediaType\":" << JsString(media)
     << ",\"isEditable\":" << (params->IsEditable() ? "true" : "false")
     << ",\"editFlags\":{\"canCut\":"
     << ((flags & CM_EDITFLAG_CAN_CUT) ? "true" : "false")
     << ",\"canCopy\":" << ((flags & CM_EDITFLAG_CAN_COPY) ? "true" : "false")
     << ",\"canPaste\":" << ((flags & CM_EDITFLAG_CAN_PASTE) ? "true" : "false")
     << "}}}";
  UiEvent("ctx", os.str());
}

// ---- загрузки --------------------------------------------------------------

namespace {

std::string HostOf(const std::string& url) {
  CefURLParts parts;
  if (CefParseURL(url, parts)) return CefString(&parts.host).ToString();
  return std::string();
}

std::string UniquePath(const std::string& dir, const std::string& name) {
  fs::path base = fs::path(dir) / fs::path(name).filename();
  if (!fs::exists(base)) return base.string();
  const std::string stem = base.stem().string();
  const std::string ext = base.extension().string();
  for (int i = 1; i < 1000; ++i) {
    fs::path p = fs::path(dir) / (stem + " (" + std::to_string(i) + ")" + ext);
    if (!fs::exists(p)) return p.string();
  }
  return base.string();
}

}  // namespace

void Shell::OnTabDownloadBefore(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefDownloadItem> item,
                                const std::string& suggested_name,
                                CefRefPtr<CefBeforeDownloadCallback> callback) {
  const std::string id = "dl" + std::to_string(item->GetId());
  std::string name = suggested_name.empty() ? "download" : suggested_name;
  pending_downloads_[id] = {callback, name};
  Log("download before id=" + id + " name=" + name + " url=" + item->GetURL().ToString() +
      " size=" + std::to_string(item->GetTotalBytes()));

  // Навигация по ссылке-загрузке не меняет страницу: возвращаем UI реальный адрес вкладки.
  if (Tab* t = FindTabByBrowser(browser->GetIdentifier())) {
    if (!t->current_url.empty() && !SameUrl(t->current_url, t->requested_url)) {
      t->requested_url = t->current_url;
      UiEvent("nav", "{\"id\":" + JsString(t->id) + ",\"url\":" +
                         JsString(t->current_url) + "}");
    }
  }

  std::ostringstream os;
  os << "{\"id\":" << JsString(id) << ",\"filename\":" << JsString(name)
     << ",\"size\":" << item->GetTotalBytes()
     << ",\"url\":" << JsString(item->GetURL().ToString())
     << ",\"host\":" << JsString(HostOf(item->GetURL().ToString())) << "}";
  UiEvent("dlprompt", os.str());
}

void Shell::DownloadDecision(const std::string& id, const std::string& action) {
  auto it = pending_downloads_.find(id);
  if (it == pending_downloads_.end()) return;
  PendingDownload pd = it->second;
  pending_downloads_.erase(it);
  if (action == "save" || action == "saveAs") {
    std::error_code ec;
    fs::create_directories(platform::DownloadsDir(), ec);
    const std::string path = UniquePath(platform::DownloadsDir(), pd.filename);
    Log("download decision id=" + id + " action=" + action + " path=" + path +
        (ec ? " mkdir_error=" + ec.message() : std::string()));
    pd.callback->Continue(path, action == "saveAs");
  }
  // "cancel": callback освобождается без Continue — загрузка отменяется.
}

// ---- полноэкранный режим контента ------------------------------------------

void Shell::OnTabFullscreen(CefRefPtr<CefBrowser> browser, bool on) {
  CEF_REQUIRE_UI_THREAD();
  Tab* t = FindTabByBrowser(browser ? browser->GetIdentifier() : -1);
  if (!t) return;
  Log("fullscreen tab=" + t->id + " on=" + (on ? "1" : "0"));
  UiEvent("fullscreen",
          "{\"id\":" + JsString(t->id) + ",\"on\":" + (on ? "true" : "false") + "}");
}

void Shell::SetWindowFullscreen(bool on) {
  CEF_REQUIRE_UI_THREAD();
  if (window_) window_->SetFullscreen(on);
}

// ---- расширения -------------------------------------------------------------

void Shell::ExtList(CefRefPtr<CefMessageRouterBrowserSide::Callback> cb) {
  CEF_REQUIRE_UI_THREAD();
  cb->Success(BuildExtListJson());
}

void Shell::ExtPushList() {
  CEF_REQUIRE_UI_THREAD();
  UiEvent("ext", BuildExtListJson());
}

void Shell::ExtToast(const std::string& text, bool err) {
  CEF_REQUIRE_UI_THREAD();
  UiEvent("ext-toast",
          "{\"text\":" + JsString(text) + ",\"err\":" + (err ? "true" : "false") + "}");
}

void Shell::ExtRemove(const std::string& id) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefRequestContext> ctx = CefRequestContext::GetGlobalContext();
  if (!ctx || id.empty()) return;
  CefRefPtr<CefExtension> e = ctx->GetExtension(id);
  if (e) e->Remove();
}

namespace {
class ExtPickCallback : public CefRunFileDialogCallback {
 public:
  ExtPickCallback() = default;
  void OnFileDialogDismissed(const std::vector<CefString>& file_paths) override {
    for (const auto& p : file_paths) Shell::Get().ExtInstall(p.ToString());
  }

 private:
  IMPLEMENT_REFCOUNTING(ExtPickCallback);
  DISALLOW_COPY_AND_ASSIGN(ExtPickCallback);
};
}  // namespace

void Shell::ExtPick() {
  CEF_REQUIRE_UI_THREAD();
  if (!ui_browser_) return;
  std::vector<CefString> filters;
  filters.push_back(CefString("*.crx"));
  filters.push_back(CefString("*.zip"));
  ui_browser_->GetHost()->RunFileDialog(
      FILE_DIALOG_OPEN, CefString::CreateASCII("Установить расширение"),
      CefString(), filters, new ExtPickCallback());
}

void Shell::ExtInstall(const std::string& src) {
  CEF_REQUIRE_UI_THREAD();
  std::string s = src;
  const auto b = s.find_first_not_of(" \t\r\n");
  const auto e = s.find_last_not_of(" \t\r\n");
  s = (b == std::string::npos) ? "" : s.substr(b, e - b + 1);
  if (s.empty()) {
    ExtToast("Вставьте ID расширения, ссылку из Chrome Web Store или путь к .crx", true);
    return;
  }

  // Локальный файл: путь или file:// URL.
  std::string path;
  if (s.rfind("file://", 0) == 0) {
    CefURLParts parts;
    if (CefParseURL(s, parts)) path = CefString(&parts.path).ToString();
  } else if (s[0] == '/' || s.rfind("./", 0) == 0 ||
             (s.size() > 2 && s[1] == ':' && (s[2] == '/' || s[2] == '\\'))) {
    path = s;
  }
  if (!path.empty()) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
      ExtToast("Файл не найден: " + path, true);
      return;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    ExtInstallBytes(ss.str(), path);
    return;
  }

  // ID расширения: 32 символа a-p (голосом или из ссылки магазина).
  const auto is_id = [](const std::string& x) {
    if (x.size() != 32) return false;
    for (char c : x) {
      if (c < 'a' || c > 'p') return false;
    }
    return true;
  };
  std::string id;
  if (is_id(s)) {
    id = s;
  } else {
    for (size_t i = 0; i + 32 <= s.size(); ++i) {
      if (is_id(s.substr(i, 32))) {
        id = s.substr(i, 32);
        break;
      }
    }
  }
  if (id.empty()) {
    if (s.rfind("http", 0) == 0 && s.find(".crx") != std::string::npos) {
      CefRefPtr<CrxDownload> dl = new CrxDownload(s);
      dl->Start(s);
      return;
    }
    ExtToast("Нужен ID расширения (32 символа) или ссылка из Chrome Web Store", true);
    return;
  }
  const std::string url =
      "https://clients2.google.com/service/update2/crx?response=redirect"
      "&prodversion=131.0&acceptformat=crx2,crx3&x=id%3D" + id +
      "%26installsource%3Dondemand%26uc";
  CefRefPtr<CrxDownload> dl = new CrxDownload(id);
  dl->Start(url);
}

void Shell::ExtInstallBytes(std::string bytes, const std::string& origin) {
  CEF_REQUIRE_UI_THREAD();
  Log("ext install bytes=" + std::to_string(bytes.size()) + " from=" + origin);
  const size_t off = CrxZipOffset(bytes);
  if (off >= bytes.size()) {
    ExtToast("Пакет не похож на CRX/ZIP расширения", true);
    return;
  }
  CefRefPtr<CefRequestContext> ctx = CefRequestContext::GetGlobalContext();
  if (!ctx) {
    ExtToast("Нет профиля для установки расширений", true);
    return;
  }
  const fs::path cache = ctx->GetCachePath().ToString();
  if (cache.empty()) {
    ExtToast("Путь профиля не определён", true);
    return;
  }
  const fs::path root = cache / "Extensions";
  std::error_code ec;
  fs::create_directories(root, ec);
  static int seq = 0;
  fs::path tmp = root / ("_tmp" + std::to_string(++seq));
  fs::create_directories(tmp, ec);
  const std::string zip(bytes.begin() + static_cast<std::ptrdiff_t>(off), bytes.end());
  if (!UnzipBytesTo(zip, tmp)) {
    ExtToast("Не удалось распаковать архив расширения", true);
    fs::remove_all(tmp, ec);
    return;
  }
  std::ifstream mf(tmp / "manifest.json", std::ios::binary);
  if (!mf) {
    ExtToast("В архиве нет manifest.json — это не расширение", true);
    fs::remove_all(tmp, ec);
    return;
  }
  std::ostringstream mss;
  mss << mf.rdbuf();
  CefRefPtr<CefValue> v = CefParseJSON(mss.str(), JSON_PARSER_RFC);
  std::string name, ver;
  if (v && v->GetType() == VTYPE_DICTIONARY) {
    CefRefPtr<CefDictionaryValue> d = v->GetDictionary();
    if (d->HasKey("name") && d->GetType("name") == VTYPE_STRING)
      name = d->GetString("name").ToString();
    if (d->HasKey("version") && d->GetType("version") == VTYPE_STRING)
      ver = d->GetString("version").ToString();
  }
  if (name.empty()) name = "extension";
  std::string slug;
  for (char c : name) {
    if (isalnum(static_cast<unsigned char>(c))) slug += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    else if (!slug.empty() && slug.back() != '-') slug += '-';
  }
  if (slug.empty() || slug.back() == '-') slug += "ext";
  fs::path dest = root / slug;
  for (int uniq = 2; fs::exists(dest, ec) && uniq < 50; ++uniq)
    dest = root / (slug + "-" + std::to_string(uniq));
  fs::rename(tmp, dest, ec);
  if (ec) {
    ExtToast("Не удалось разместить расширение в профиле", true);
    fs::remove_all(tmp, ec);
    return;
  }
  ExtToast("Устанавливаю: " + name + (ver.empty() ? "" : " v" + ver), false);
  ctx->LoadExtension(dest.ToString(), new ShellExtHandler());
}

void Shell::OnTabDownloadUpdated(CefRefPtr<CefDownloadItem> item) {
  const std::string id = "dl" + std::to_string(item->GetId());
  std::string state = "progressing";
  if (item->IsComplete()) {
    state = "completed";
  } else if (item->IsCanceled()) {
    state = "cancelled";
  } else if (!item->IsInProgress()) {
    state = "interrupted";
  }
  std::string name = item->GetSuggestedFileName().ToString();
  const std::string path = item->GetFullPath().ToString();
  if (state != "progressing")
    Log("download update id=" + id + " state=" + state + " bytes=" +
        std::to_string(item->GetReceivedBytes()) + " reason=" +
        std::to_string(static_cast<int>(item->GetInterruptReason())) + " path=" + path);
  if (name.empty() && !path.empty()) name = fs::path(path).filename().string();
  if (name.empty()) name = "download";

  std::ostringstream os;
  os << "{\"id\":" << JsString(id) << ",\"filename\":" << JsString(name)
     << ",\"state\":" << JsString(state)
     << ",\"bytes\":" << item->GetReceivedBytes()
     << ",\"total\":" << item->GetTotalBytes()
     << ",\"path\":" << JsString(path) << "}";
  UiEvent("download", os.str());
}

}  // namespace shelter
