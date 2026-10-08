#include "src/shell.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

#if !defined(OS_WIN)
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include <cctype>
#include <cstring>
#include <regex>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_auth_callback.h"
#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/cef_request_context_handler.h"
#include "include/cef_stream.h"
#include "include/cef_urlrequest.h"
#include "include/cef_zip_reader.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_display.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "src/clients.h"
#include "src/blocker.h"
#include "src/common.h"
#include "src/netguard.h"
#include "src/key_map.h"
#include "src/platform.h"
#include "src/security_paths.h"
#include "src/ui_scheme.h"

namespace fs = std::filesystem;

namespace shelter {

namespace {

// Chrome initialises disk-based profiles asynchronously. Browsers must only
// be attached to a request context after that completes (CEF delivers the
// notification through CefRequestContextHandler::OnRequestContextInitialized).
class ShellContextHandler : public CefRequestContextHandler {
 public:
  explicit ShellContextHandler(std::string partition)
      : partition_(std::move(partition)) {}
  void OnRequestContextInitialized(
      CefRefPtr<CefRequestContext> /*context*/) override {
    CEF_REQUIRE_UI_THREAD();
    Shell::Get().OnContextReady(partition_);
  }

 private:
  std::string partition_;
  IMPLEMENT_REFCOUNTING(ShellContextHandler);
};

// fs::path::u8string() yields std::u8string (char8_t) in C++20, which does
// not convert to std::string implicitly. Centralise the conversion.
std::string PathToUtf8(const fs::path& path) {
  const auto value = path.u8string();
#if defined(__cpp_char8_t)
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
  return value;
#endif
}

constexpr int kMinWidth = 960;
constexpr int kMinHeight = 620;
constexpr size_t kMaxExtensionArchiveBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxExtensionFileBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxExtensionExpandedBytes = 256u * 1024u * 1024u;
constexpr size_t kMaxExtensionEntries = 20000;
constexpr size_t kMaxExtensionManifestBytes = 1u * 1024u * 1024u;
constexpr size_t kMaxExtensionRuntimeBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxExtensionContentScripts = 128;
constexpr size_t kMaxExtensionMatchPatterns = 64;
constexpr size_t kMaxInstalledExtensions = 256;
constexpr std::uintmax_t kMaxPendingWipeListBytes = 64u * 1024u;
constexpr size_t kMaxPendingWipeEntries = 1024;

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

// ---- расширения: CRX -> распаковка -> собственный runtime content-scripts --
// CEF вырезал API расширений (~M127), поэтому Shelter сам выполняет ту часть
// модели Chromium-расширений, которая не требует Chrome-UI: content_scripts
// (JS/CSS из пакета) внедряются в главные фреймы вкладок по match-паттернам.
// Фоновые service worker и chrome.* API не поддерживаются — об этом честно
// написано на странице «Расширения».

struct ExtContentScript {
  std::vector<std::string> matches;
  std::string js;    // объединённый исходник js-файлов
  std::string css;   // объединённый исходник css-файлов
  bool at_start = false;  // run_at: document_start
};

struct ExtEntry {
  std::string id;
  std::string name;
  std::string ver;
  std::string path;
  std::vector<ExtContentScript> scripts;
};

std::map<std::string, ExtEntry> g_exts;
bool g_exts_scanned = false;

bool ReadFileStr(const fs::path& path, size_t max_bytes, std::string* out) {
  if (!out || !security::IsRegularFileWithoutLink(path)) return false;
  std::error_code ec;
  const std::uintmax_t links = fs::hard_link_count(path, ec);
  if (ec || links != 1) return false;
  const std::uintmax_t size = fs::file_size(path, ec);
  if (ec || size > max_bytes) return false;

  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  out->resize(static_cast<size_t>(size));
  if (size != 0) {
    file.read(out->data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(file.gcount()) != size) {
      out->clear();
      return false;
    }
  }
  return true;
}

// match-pattern (<scheme>://<host>/<path>, <all_urls>) -> regex.
bool ExtMatch(const std::string& pattern, const std::string& url) {
  if (pattern == "<all_urls>")
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
  const auto sep = pattern.find("://");
  if (sep == std::string::npos) return false;
  std::string scheme = pattern.substr(0, sep);
  const std::string rest = pattern.substr(sep + 3);
  const auto slash = rest.find('/');
  const std::string host =
      slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
  if (path.empty()) path = "/";
  CefURLParts parts;
  if (!CefParseURL(url, parts)) return false;
  const std::string uscheme = CefString(&parts.scheme).ToString();
  const std::string uhost = CefString(&parts.host).ToString();
  std::string upath = CefString(&parts.path).ToString();
  if (upath.empty()) upath = "/";
  if (scheme != "*" && scheme != uscheme) return false;
  const auto to_regex = [](const std::string& s) {
    std::string r;
    for (char c : s) {
      if (c == '*') r += ".*";
      else if (strchr(".+?[]^$(){}|\\", c)) { r += '\\'; r += c; }
      else r += c;
    }
    return r;
  };
  std::string hpat = to_regex(host);
  if (!host.empty() && host[0] == '*') {  // *.host или *
    hpat = to_regex(host.substr(1));
    hpat = "([a-z0-9.-]+\\.)?" + hpat;
    if (host == "*") hpat = ".*";
  }
  if (!std::regex_match(uhost, std::regex(hpat))) return false;
  return std::regex_match(upath, std::regex(to_regex(path)));
}

bool ExtScriptApplies(const ExtContentScript& cs, const std::string& url) {
  for (const auto& m : cs.matches) {
    if (ExtMatch(m, url)) return true;
  }
  return false;
}

void LoadExtFromDir(const fs::path& dir) {
  if (!security::IsDirectoryWithoutLink(dir)) return;
  const std::string extension_id = dir.filename().string();
  if (!security::IsSafeProfileId(extension_id)) return;

  std::string manifest;
  if (!ReadFileStr(dir / "manifest.json", kMaxExtensionManifestBytes,
                   &manifest)) {
    return;
  }
  CefRefPtr<CefValue> v = CefParseJSON(manifest, JSON_PARSER_RFC);
  if (!v || v->GetType() != VTYPE_DICTIONARY) return;
  CefRefPtr<CefDictionaryValue> d = v->GetDictionary();
  ExtEntry e;
  e.path = PathToUtf8(dir);
  e.id = extension_id;
  if (d->HasKey("name") && d->GetType("name") == VTYPE_STRING)
    e.name = d->GetString("name").ToString();
  if (e.name.empty()) e.name = e.id;
  if (d->HasKey("version") && d->GetType("version") == VTYPE_STRING)
    e.ver = d->GetString("version").ToString();

  if (d->HasKey("content_scripts") &&
      d->GetType("content_scripts") == VTYPE_LIST) {
    CefRefPtr<CefListValue> list = d->GetList("content_scripts");
    if (list->GetSize() > kMaxExtensionContentScripts) return;
    size_t runtime_bytes = 0;
    for (size_t i = 0; i < list->GetSize(); ++i) {
      if (list->GetType(i) != VTYPE_DICTIONARY) continue;
      CefRefPtr<CefDictionaryValue> cs = list->GetDictionary(i);
      ExtContentScript out;
      if (cs->HasKey("run_at") && cs->GetType("run_at") == VTYPE_STRING)
        out.at_start = cs->GetString("run_at").ToString() == "document_start";
      if (cs->HasKey("matches") && cs->GetType("matches") == VTYPE_LIST) {
        CefRefPtr<CefListValue> matches = cs->GetList("matches");
        if (matches->GetSize() > kMaxExtensionMatchPatterns) continue;
        for (size_t k = 0; k < matches->GetSize(); ++k) {
          if (matches->GetType(k) != VTYPE_STRING) continue;
          const std::string pattern = matches->GetString(k).ToString();
          if (!pattern.empty() && pattern.size() <= 2048)
            out.matches.push_back(pattern);
        }
      }
      if (out.matches.empty()) continue;

      bool over_budget = false;
      const auto append_files = [&](const char* key, bool is_js) {
        if (!cs->HasKey(key) || cs->GetType(key) != VTYPE_LIST) return;
        CefRefPtr<CefListValue> files = cs->GetList(key);
        if (files->GetSize() > kMaxExtensionEntries) {
          over_budget = true;
          return;
        }
        for (size_t k = 0; k < files->GetSize(); ++k) {
          if (files->GetType(k) != VTYPE_STRING) continue;
          std::string rel = files->GetString(k).ToString();
          if (!security::NormalizeSafeZipEntryName(&rel)) continue;
          const fs::path source = (dir / fs::u8path(rel)).lexically_normal();
          if (!security::IsPathWithin(dir, source)) continue;
          std::string content;
          if (!ReadFileStr(source, kMaxExtensionFileBytes, &content)) continue;
          if (content.size() > kMaxExtensionRuntimeBytes - runtime_bytes) {
            over_budget = true;
            return;
          }
          runtime_bytes += content.size();
          if (is_js) {
            if (!out.js.empty()) {
              out.js.push_back(static_cast<char>(10));
              out.js.push_back(';');
              out.js.push_back(static_cast<char>(10));
            }
            out.js += content;
          } else {
            if (!out.css.empty()) out.css.push_back(static_cast<char>(10));
            out.css += content;
          }
        }
      };
      append_files("js", true);
      if (!over_budget) append_files("css", false);
      if (over_budget) return;
      if (!out.js.empty() || !out.css.empty())
        e.scripts.push_back(std::move(out));
    }
  }
  g_exts[e.id] = std::move(e);
}

fs::path ExtRootDir() {
  CefRefPtr<CefRequestContext> ctx = CefRequestContext::GetGlobalContext();
  if (!ctx) return fs::path();
  const std::string cache = ctx->GetCachePath().ToString();
  if (cache.empty()) return fs::path();
  return fs::path(cache) / "Extensions";
}

void ExtEnsureScanned() {
  if (g_exts_scanned) return;
  g_exts_scanned = true;
  const fs::path root = ExtRootDir();
  if (root.empty() || !security::IsDirectoryWithoutLink(root)) return;
  std::error_code ec;
  fs::directory_iterator it(root, ec);
  const fs::directory_iterator end;
  if (ec) return;
  size_t scanned = 0;
  while (it != end) {
    const fs::path dir = it->path();
    const std::string id = dir.filename().string();
    if (id.rfind("_tmp", 0) != 0 && security::IsSafeProfileId(id) &&
        security::IsDirectoryWithoutLink(dir)) {
      if (scanned >= kMaxInstalledExtensions) break;
      ++scanned;
      LoadExtFromDir(dir);
    }
    it.increment(ec);
    if (ec) return;
  }
}

std::string BuildExtListJson() {
  ExtEnsureScanned();
  std::ostringstream os;
  os << "{\"list\":[";
  bool first = true;
  for (const auto& kv : g_exts) {
    const ExtEntry& e = kv.second;
    if (!first) os << ",";
    first = false;
    os << "{\"id\":" << JsString(e.id) << ",\"name\":" << JsString(e.name)
       << ",\"ver\":" << JsString(e.ver) << ",\"path\":" << JsString(e.path)
       << ",\"cs\":" << e.scripts.size() << "}";
  }
  os << "]}";
  return os.str();
}

void InjectExtScripts(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      bool start_phase) {
  if (!browser || !frame || !frame->IsMain()) return;
  ExtEnsureScanned();
  if (g_exts.empty()) return;
  const std::string url = frame->GetURL().ToString();
  if (url.empty() || url == "about:blank") return;
  for (const auto& kv : g_exts) {
    for (const auto& cs : kv.second.scripts) {
      if (cs.at_start != start_phase) continue;
      if (!ExtScriptApplies(cs, url)) continue;
      if (!cs.css.empty()) {
        const std::string js =
            "(function(){var s=document.createElement('style');s.textContent=" +
            JsString(cs.css) +
            ";(document.head||document.documentElement).appendChild(s);})();";
        frame->ExecuteJavaScript(js, url, 0);
      }
      if (!cs.js.empty()) frame->ExecuteJavaScript(cs.js, url, 0);
    }
  }
}

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
    if (too_large_) {
      Shell::Get().ExtToast("Архив расширения превышает лимит 64 МиБ", true);
    } else if (request->GetRequestStatus() == UR_SUCCESS && !buf_.empty()) {
      Shell::Get().ExtInstallBytes(std::move(buf_), origin_);
    } else {
      Shell::Get().ExtToast(
          "Не удалось скачать пакет расширения (сеть или магазин недоступны)",
          true);
    }
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data,
                      size_t data_length) override {
    if (too_large_) return;
    if (data_length > kMaxExtensionArchiveBytes - buf_.size()) {
      too_large_ = true;
      buf_.clear();
      if (request) request->Cancel();
      return;
    }
    buf_.append(static_cast<const char*>(data), data_length);
  }
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&,
                          const CefString&,
                          CefRefPtr<CefAuthCallback>) override {
    return false;
  }

 private:
  std::string origin_;
  std::string buf_;
  bool too_large_ = false;
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

bool UnzipBytesTo(const std::string& bytes, const fs::path& dir) {
  if (bytes.empty() || bytes.size() > kMaxExtensionArchiveBytes ||
      !security::IsDirectoryWithoutLink(dir)) {
    return false;
  }
  std::error_code ec;
  const fs::path root = fs::absolute(dir, ec).lexically_normal();
  if (ec) return false;

  CefRefPtr<CefStreamReader> sr = CefStreamReader::CreateForData(
      const_cast<char*>(bytes.data()), bytes.size());
  if (!sr) return false;
  CefRefPtr<CefZipReader> zr = CefZipReader::Create(sr);
  if (!zr || !zr->MoveToFirstFile()) return false;

  bool any = false;
  size_t entries = 0;
  size_t expanded_bytes = 0;
  do {
    if (++entries > kMaxExtensionEntries) return false;
    std::string name = zr->GetFileName().ToString();
    if (!security::NormalizeSafeZipEntryName(&name)) return false;

    const fs::path fp = (root / fs::u8path(name)).lexically_normal();
    if (!security::IsPathWithin(root, fp)) return false;
    if (name.back() == '/') {
      ec.clear();
      fs::create_directories(fp, ec);
      if (ec || !security::IsDirectoryWithoutLink(fp)) return false;
      continue;
    }

    ec.clear();
    fs::create_directories(fp.parent_path(), ec);
    if (ec || !security::IsDirectoryWithoutLink(fp.parent_path())) return false;
    std::error_code status_error;
    const fs::file_status existing = fs::symlink_status(fp, status_error);
    if (status_error && status_error != std::errc::no_such_file_or_directory)
      return false;
    if (!status_error && existing.type() != fs::file_type::not_found)
      return false;  // Reject duplicate names and file/directory collisions.

    if (!zr->OpenFile(CefString())) return false;
    std::ofstream out(fp, std::ios::binary | std::ios::out);
    if (!out) {
      zr->CloseFile();
      return false;
    }

    char chunk[65536];
    size_t file_bytes = 0;
    bool limit_exceeded = false;
    size_t n = 0;
    while ((n = zr->ReadFile(chunk, sizeof(chunk))) > 0) {
      if (n > kMaxExtensionFileBytes - file_bytes ||
          n > kMaxExtensionExpandedBytes - expanded_bytes) {
        limit_exceeded = true;
        break;
      }
      out.write(chunk, static_cast<std::streamsize>(n));
      if (!out) {
        limit_exceeded = true;
        break;
      }
      file_bytes += n;
      expanded_bytes += n;
    }
    out.close();
    zr->CloseFile();
    if (limit_exceeded || !out) return false;
    any = true;
  } while (zr->MoveToNextFile());
  return any;
}

bool ReadExtensionFileBounded(const fs::path& path, std::string* out) {
  if (!out || !security::IsRegularFileWithoutLink(path)) return false;
  std::error_code ec;
  const std::uintmax_t size = fs::file_size(path, ec);
  if (ec || size == 0 || size > kMaxExtensionArchiveBytes) return false;
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  out->resize(static_cast<size_t>(size));
  file.read(out->data(), static_cast<std::streamsize>(size));
  return static_cast<std::uintmax_t>(file.gcount()) == size;
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

void Shell::PrepareForShutdown() {
  CEF_REQUIRE_UI_THREAD();
  // Release persistent request contexts before CefShutdown so SQLite/cache
  // handles are closed before the profile directory is removed.
  pending_downloads_.clear();
  accepted_downloads_.clear();
  active_downloads_.clear();
  temp_downloads_.clear();
  tabs_.clear();
  contexts_.clear();
  context_ready_.clear();
  context_waiters_.clear();
  popup_windows_.clear();
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

void Shell::OnUiCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  ui_browser_ = browser;
  ui_browser_id_.store(browser ? browser->GetIdentifier() : -1,
                       std::memory_order_release);
}

void Shell::OnUiClosed(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (browser && !IsUiBrowserId(browser->GetIdentifier())) return;
  ui_browser_id_.store(-1, std::memory_order_release);
  ui_browser_ = nullptr;
}

bool Shell::IsUiBrowser(CefRefPtr<CefBrowser> browser) const {
  return browser && IsUiBrowserId(browser->GetIdentifier());
}

bool Shell::IsUiBrowserId(int browser_id) const {
  return browser_id >= 0 &&
         ui_browser_id_.load(std::memory_order_acquire) == browser_id;
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
    const std::string id = SanitizeForPath(partition.substr(8));
    const fs::path user_data = fs::u8path(platform::UserDataDir());
    // Chrome accepts a disk profile only when its path is a direct child of
    // the user-data directory (root_cache_path), so keep profiles flat.
    const fs::path profile = user_data / id;
    if (security::IsSafeProfileId(id) &&
        !security::IsReservedProfileName(id) &&
        security::EnsureDirectoryWithoutLink(user_data) &&
        security::IsPathWithin(user_data, profile) &&
        security::EnsureDirectoryWithoutLink(profile)) {
      CefString(&rs.cache_path) = PathToUtf8(profile);
      rs.persist_session_cookies = true;
    }
    // Unsafe/unavailable paths deliberately fall back to an in-memory context.
  }
  // temp:* и прочее — cache_path пуст: контекст только в памяти (без следов на диске).
  // Хэндлер сообщает, когда контекст (и его профиль) проинициализирован:
  // до этого привязывать к нему браузеры нельзя.
  CefRefPtr<CefRequestContext> ctx =
      CefRequestContext::CreateContext(rs, new ShellContextHandler(partition));
  contexts_[partition] = ctx;
  context_ready_[partition] = false;
  return ctx;
}

bool Shell::ContextReady(const std::string& partition) const {
  const auto it = context_ready_.find(partition);
  return it != context_ready_.end() && it->second;
}

void Shell::OnContextReady(const std::string& partition) {
  CEF_REQUIRE_UI_THREAD();
  auto ready = context_ready_.find(partition);
  if (ready == context_ready_.end()) return;  // контекст уже выброшен
  ready->second = true;
  // Профиль инициализирован — применяем шифрованный DNS, если он включён
  // (каждое пространство получает свою копию настроек профиля).
  auto ctx = contexts_.find(partition);
  if (ctx != contexts_.end()) netguard::ApplyDohPrefs(ctx->second);
  auto waiters = context_waiters_.find(partition);
  if (waiters == context_waiters_.end()) return;
  std::vector<std::string> ids = std::move(waiters->second);
  context_waiters_.erase(waiters);
  for (const std::string& id : ids) {
    Tab* t = FindTab(id);
    if (t && t->awaiting_context) FinishTabBrowser(t);
  }
}

void Shell::FinishTabBrowser(Tab* t) {
  CEF_REQUIRE_UI_THREAD();
  if (!t || t->view || !window_) return;
  auto ctx = contexts_.find(t->partition);
  if (ctx == contexts_.end()) return;
  t->awaiting_context = false;

  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(255, 255, 255, 255);
  t->view = CefBrowserView::CreateBrowserView(
      t->client, t->requested_url, settings, nullptr, ctx->second,
      new ShellBrowserViewDelegate());
  const bool can_activate =
      !CefCommandLine::GetGlobalCommandLine()->HasSwitch("tab-no-activate");
  t->overlay = window_->AddOverlayView(t->view, CEF_DOCKING_MODE_CUSTOM,
                                       can_activate);
  t->overlay->SetVisible(false);
  if (t->has_pending_layout) {
    const CefRect rect = t->pending_rect;
    const bool visible = t->pending_visible;
    t->has_pending_layout = false;
    LayoutTab(t, rect, visible);
  }
  if (t->pending_focus && active_tab_ == t->id) t->view->RequestFocus();
  t->pending_focus = false;
  if (CefRefPtr<CefBrowser> b = t->view->GetBrowser()) {
    OnTabCreated(b, t->id);
  }
}

void Shell::ReleaseContextIfUnused(const std::string& partition) {
  if (partition.rfind("temp:", 0) != 0) return;  // persist-контексты держим
  for (auto& kv : tabs_) {
    if (kv.second.partition == partition) return;
  }
  contexts_.erase(partition);
  context_ready_.erase(partition);
}

void Shell::QueueWipe(const std::string& partition) {
  if (partition.rfind("persist:", 0) != 0) return;
  const std::string id = SanitizeForPath(partition.substr(8));
  if (!security::IsSafeProfileId(id) || security::IsReservedProfileName(id)) {
    return;
  }

  const fs::path user_data = fs::u8path(platform::UserDataDir());
  if (!security::EnsureDirectoryWithoutLink(user_data)) {
    return;
  }
  const fs::path profile = user_data / id;
  if (!security::IsPathWithin(user_data, profile) ||
      !security::EnsureDirectoryWithoutLink(profile)) {
    return;
  }

  const fs::path list = user_data / "pending-wipe.txt";
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(list, ec);
  const bool missing = ec == std::errc::no_such_file_or_directory ||
                       (!ec && status.type() == fs::file_type::not_found);
  if (ec && !missing) return;
  if (!missing) {
    if (!security::IsRegularFileWithoutLink(list)) return;
    const std::uintmax_t size = fs::file_size(list, ec);
    if (ec || size > kMaxPendingWipeListBytes) return;
    const std::uintmax_t links = fs::hard_link_count(list, ec);
    if (ec || links != 1) return;  // Never append through an external hard link.

    std::ifstream existing(list, std::ios::binary);
    if (!existing) return;
    std::string line;
    size_t lines = 0;
    while (std::getline(existing, line)) {
      if (line == id) return;
      if (++lines >= kMaxPendingWipeEntries ||
          size + id.size() + 1 > kMaxPendingWipeListBytes) {
        return;
      }
    }
    if (!existing.eof()) return;
  } else if (id.size() + 1 > kMaxPendingWipeListBytes) {
    return;
  }

  std::ofstream out(list, std::ios::binary | std::ios::app);
  if (!out) return;
  out << id << '\n';
  out.close();
#if !defined(OS_WIN)
  ::chmod(list.c_str(), 0600);
#endif
}

// Профили, помеченные «сжечь», удаляются при следующем старте. Все имена —
// один компонент из allowlist; ссылки/reparse points не обходятся, а hard-link
// файлы не перезаписываются (их данные могут принадлежать файлу вне профиля).
// Затирание — best effort; на APFS/SSD физическую гарантию дают FileVault/TRIM,
// а не повторная запись поверх файла.
void Shell::ApplyPendingWipes() {
  const fs::path user_data = fs::u8path(platform::UserDataDir());
  const fs::path list = user_data / "pending-wipe.txt";
  if (!security::IsDirectoryWithoutLink(user_data)) return;

  // Pre-1.0.166 builds nested profiles under Profiles/. Chrome only accepts
  // profiles that are direct children of the user-data directory and rejected
  // those paths, so wipe the legacy container wholesale on upgrade.
  std::error_code ec;
  const fs::path legacy_profiles = user_data / "Profiles";
  const fs::file_status legacy_status = fs::symlink_status(legacy_profiles, ec);
  const bool legacy_missing =
      ec == std::errc::no_such_file_or_directory ||
      (!ec && legacy_status.type() == fs::file_type::not_found);
  if (!ec && !legacy_missing) {
    security::RemoveTreeWithoutFollowingLinks(user_data, legacy_profiles);
  }

  if (!security::IsRegularFileWithoutLink(list)) return;

  const std::uintmax_t list_size = fs::file_size(list, ec);
  if (ec || list_size > kMaxPendingWipeListBytes) return;

  std::ifstream f(list, std::ios::binary);
  if (!f) return;
  std::vector<std::string> ids;
  std::string line;
  size_t lines = 0;
  while (std::getline(f, line)) {
    if (++lines > kMaxPendingWipeEntries) return;
    if (security::IsSafeProfileId(line) &&
        !security::IsReservedProfileName(line) &&
        std::find(ids.begin(), ids.end(), line) == ids.end()) {
      ids.push_back(line);
    }
  }
  if (!f.eof()) return;
  f.close();

  bool all_removed = true;
  for (const std::string& id : ids) {
    const fs::path profile = user_data / id;
    if (!security::IsPathWithin(user_data, profile) ||
        !security::RemoveTreeWithoutFollowingLinks(user_data, profile)) {
      all_removed = false;
    }
  }
  if (!all_removed) return;  // Keep the bounded list for a later retry.

  // Scrub/remove only the regular list file itself; a replaced symlink is never followed.
  if (!security::IsRegularFileWithoutLink(list)) return;
  security::OverwriteFileIfUnshared(list);
  ec.clear();
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

std::string Shell::TabIdForBrowser(int browser_id) {
  if (browser_id <= 0) return std::string();
  Tab* t = FindTabByBrowser(browser_id);
  return t ? t->id : std::string();
}

Tab* Shell::CreateTab(const std::string& id, const std::string& partition,
                      const std::string& url, double zoom) {
  Tab tab;
  tab.id = id;
  tab.partition = partition;
  tab.requested_url = url;
  tab.client = new TabClient(id);
  zoom_ = zoom;

  // Создание контекста может запустить асинхронное создание профиля Chrome.
  ContextFor(partition);
  tabs_[id] = std::move(tab);
  Tab* t = &tabs_[id];
  if (ContextReady(partition)) {
    FinishTabBrowser(t);
  } else {
    // Браузер будет создан из OnContextReady; до тех пор запоминаем запросы
    // геометрии/фокуса из моста.
    t->awaiting_context = true;
    context_waiters_[partition].push_back(id);
  }
  return t;
}

void Shell::OnTabCreated(CefRefPtr<CefBrowser> browser, const std::string& id) {
  Tab* t = FindTab(id);
  if (!t || t->browser) return;
  t->browser = browser;
  t->browser_id = browser->GetIdentifier();
  browser->GetHost()->SetZoomLevel(std::log(zoom_) / std::log(1.2));
  PushFpState(browser);  // рендерер узнаёт состояние «Анти-отпечатка» до загрузки
  PushCookieState(browser);  // и состояние автосогласия cookies
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
  auto w = context_waiters_.find(tab.partition);
  if (w != context_waiters_.end()) {
    auto& ids = w->second;
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    if (ids.empty()) context_waiters_.erase(w);
  }
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
  if (!tab) return;
  if (!tab->view || !tab->overlay || !tab->overlay->IsValid()) {
    // Браузер ещё создаётся (ждём профиль): геометрия придёт снова из моста,
    // но сохраним последнюю, чтобы применить сразу после создания.
    if (!tab->view && rect.width > 0 && rect.height > 0) {
      tab->pending_rect = rect;
      tab->pending_visible = visible;
      tab->has_pending_layout = true;
    }
    return;
  }
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
  const fs::path dir = fs::path(platform::UserDataDir());
  const fs::path p = dir / "shelter.log";
  // Ретенция: журнал не растёт бесконечно и не копит историю сессий. Один
  // предыдущий файл — для разбора последнего сбоя, дальше старые следы уходят.
  constexpr std::uintmax_t kMaxLogBytes = 512 * 1024;
  std::error_code ec;
  if (fs::file_size(p, ec) > kMaxLogBytes && !ec) {
    const fs::path prev = dir / "shelter.log.1";
    std::error_code rec;
    fs::remove(prev, rec);
    fs::rename(p, prev, rec);
  }
  std::ofstream f(p, std::ios::app);
  if (!f) return;
  f << line << "\n";
  f.close();
#if !defined(OS_WIN)
  // В записях нет адресов и путей (см. ExtLabel), но журнал всё равно личный:
  // держим 0600 — не шире владельца.
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

// В shelter.log попадают только обезличенные метки: расширение файла помогает
// разбирать сбои, но не выдаёт ни путь, ни полное имя. Всё остальное — мусор.
std::string ExtLabel(const std::string& name) {
  std::string ext = PathToUtf8(fs::u8path(name).extension());
  if (ext.size() > 8) ext.resize(8);
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.') c = '_';
  }
  return ext.empty() ? std::string("-") : ext;
}

std::string UniquePath(const std::string& dir, const std::string& name) {
  const fs::path directory = fs::u8path(dir);
  fs::path leaf = fs::u8path(name).filename();
  if (leaf.empty() || leaf == "." || leaf == "..") leaf = fs::u8path("download");
  const fs::path base = directory / leaf;
  std::error_code ec;
  if (!fs::exists(base, ec) && !ec) return PathToUtf8(base);

  const std::string stem = PathToUtf8(base.stem());
  const std::string ext = PathToUtf8(base.extension());
  for (int i = 1; i < 1000; ++i) {
    const fs::path candidate =
        directory / fs::u8path(stem + " (" + std::to_string(i) + ")" + ext);
    ec.clear();
    if (!fs::exists(candidate, ec) && !ec) return PathToUtf8(candidate);
  }
  return PathToUtf8(base);
}

// Суффикс «не подтверждённого» файла, как в современных браузерах
// (Chrome — *.crdownload, Safari — *.download): пока загрузка идёт, в папке
// назначения виден временный файл, который становится окончательным только
// после полного завершения.
constexpr char kPartialSuffix[] = ".crdownload";

// Приватная («призрачная») вкладка живёт в in-memory профиле temp:ghost-<key>.
bool IsGhostPartition(const std::string& p) {
  return p.rfind("temp:ghost-", 0) == 0;
}

}  // namespace

void Shell::OnTabDownloadBefore(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefDownloadItem> item,
                                const std::string& suggested_name,
                                CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!item || !item->IsValid() || !callback) return;

  const std::string id = "dl" + std::to_string(item->GetId());
  std::string name = suggested_name;
  if (name.empty()) name = item->GetSuggestedFileName().ToString();
  if (name.empty()) name = "download";
  // Приватная вкладка оставляет файл на диске (как инкогнито в больших
  // браузерах), но не следов в логе SHELTER: ни адреса, ни пути.
  const Tab* tab = browser ? FindTabByBrowser(browser->GetIdentifier()) : nullptr;
  const bool priv = tab && IsGhostPartition(tab->partition);
  pending_downloads_[id] = {callback, name, priv};
  if (priv) private_downloads_.insert(id);

  const std::string url = item->GetURL().ToString();
  const std::int64_t reported_size = item->GetTotalBytes();
  const std::int64_t size = reported_size > 0 ? reported_size : 0;
  Log("download before id=" + id + (priv ? " private=1" : "") +
      " ext=" + ExtLabel(name) + " size=" + std::to_string(size));

  // Навигация по ссылке-загрузке не меняет страницу: возвращаем UI реальный адрес вкладки.
  if (browser) {
    if (Tab* t = FindTabByBrowser(browser->GetIdentifier())) {
      if (!t->current_url.empty() && !SameUrl(t->current_url, t->requested_url)) {
        t->requested_url = t->current_url;
        UiEvent("nav", "{\"id\":" + JsString(t->id) + ",\"url\":" +
                           JsString(t->current_url) + "}");
      }
    }
  }

  std::ostringstream os;
  os << "{\"id\":" << JsString(id) << ",\"filename\":" << JsString(name)
     << ",\"size\":" << size
     << ",\"url\":" << JsString(url)
     << ",\"host\":" << JsString(HostOf(url))
     << ",\"mimeType\":" << JsString(item->GetMimeType().ToString()) << "}";
  UiEvent("dlprompt", os.str());
}

bool Shell::DownloadDecision(const std::string& id, const std::string& action,
                             bool show_dialog) {
  CEF_REQUIRE_UI_THREAD();
  auto it = pending_downloads_.find(id);
  if (it == pending_downloads_.end()) return false;

  PendingDownload pd = it->second;
  pending_downloads_.erase(it);
  if (action == "cancel") {
    Log("download decision id=" + id + " action=cancel");
    // Releasing the uncontinued CEF callback cancels the pending download.
    return true;
  }
  if (action != "save" || !pd.callback) return false;

  std::error_code ec;
  const std::string directory = platform::DownloadsDir();
  fs::create_directories(fs::u8path(directory), ec);
  if (ec && !show_dialog) {
    Log("download decision id=" + id + " action=save mkdir_error=" +
        ec.message());
    return false;
  }

  const std::string path = ec && show_dialog
                               ? std::string()
                               : UniquePath(directory, pd.filename);
  accepted_downloads_[id] = pd.filename;
  std::string pass_path = path;
  if (!show_dialog && !path.empty()) {
    // Прямое сохранение: файл скачивается под временным именем
    // <имя>.crdownload и переименовывается в итоговое после завершения
    // (см. OnTabDownloadUpdated). При «Сохранить как…» путь выбирает
    // системный диалог — там временное имя не задаём.
    pass_path = path + kPartialSuffix;
    temp_downloads_[id] = {pass_path, path};
  }
  Log("download decision id=" + id + " action=save dialog=" +
      (show_dialog ? "1" : "0") + " ext=" + ExtLabel(pd.filename) +
      " name_len=" + std::to_string(pd.filename.size()) +
      (pass_path != path ? " partial=1" : std::string()) +
      (ec ? " mkdir_error=" + ec.message() : std::string()));
  // With the Save As preference enabled, CEF opens its native chooser while
  // keeping the suggested filename/path as the initial value.
  pd.callback->Continue(pass_path, show_dialog);
  return true;
}

bool Shell::DownloadControl(const std::string& id,
                            const std::string& action) {
  CEF_REQUIRE_UI_THREAD();
  auto it = active_downloads_.find(id);
  if (it == active_downloads_.end() || !it->second.callback) return false;

  // Keep a local ref: the callback can synchronously trigger an update that
  // replaces or removes the map entry.
  CefRefPtr<CefDownloadItemCallback> callback = it->second.callback;
  if (action == "pause") {
    callback->Pause();
  } else if (action == "resume") {
    callback->Resume();
  } else if (action == "cancel") {
    callback->Cancel();
  } else {
    return false;
  }
  return true;
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
  ExtEnsureScanned();
  auto it = g_exts.find(id);
  if (it == g_exts.end() || !security::IsSafeProfileId(id)) return;
  const fs::path root = ExtRootDir();
  const fs::path target = root / fs::u8path(id);
  if (root.empty() || !security::IsPathWithin(root, target) ||
      !security::RemoveTreeWithoutFollowingLinks(root, target)) {
    ExtToast("Не удалось безопасно удалить расширение", true);
    return;
  }
  const std::string name = it->second.name;
  g_exts.erase(it);
  ExtToast("Расширение удалено: " + name, false);
  ExtPushList();
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
  const CefString title(std::string("Установить расширение"));
  ui_browser_->GetHost()->RunFileDialog(FILE_DIALOG_OPEN, title, CefString(),
                                        filters, new ExtPickCallback());
}

// Открытие локального файла (Ctrl+O): реальный диалог ОС и реальный путь —
// UI получает готовый file:// URL и открывает его вкладкой.
namespace {
class FileOpenCallback : public CefRunFileDialogCallback {
 public:
  FileOpenCallback() = default;
  void OnFileDialogDismissed(const std::vector<CefString>& file_paths) override {
    if (file_paths.empty()) return;
    std::string path = file_paths[0].ToString();
    if (path.empty()) return;
    std::string url = "file://";
    if (path[0] != '/') url += "/";  // Windows: C:\… -> file:///C:/…
    for (char c : path) url += (c == '\\') ? '/' : c;
    Shell::Get().UiEvent("file-open", "{\"url\":" + JsString(url) + "}");
  }

 private:
  IMPLEMENT_REFCOUNTING(FileOpenCallback);
  DISALLOW_COPY_AND_ASSIGN(FileOpenCallback);
};
}  // namespace

void Shell::PickLocalFile() {
  CEF_REQUIRE_UI_THREAD();
  if (!ui_browser_) return;
  const CefString title(std::string("Открыть файл"));
  ui_browser_->GetHost()->RunFileDialog(FILE_DIALOG_OPEN, title, CefString(),
                                        std::vector<CefString>(),
                                        new FileOpenCallback());
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
    std::string bytes;
    if (!ReadExtensionFileBounded(fs::u8path(path), &bytes)) {
      ExtToast("Файл расширения недоступен или превышает лимит 64 МиБ", true);
      return;
    }
    ExtInstallBytes(std::move(bytes), "local file");
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
  Log("ext install bytes=" + std::to_string(bytes.size()) +
      " source=" + (origin == "local file" ? "local" : "remote"));
  if (bytes.empty() || bytes.size() > kMaxExtensionArchiveBytes) {
    ExtToast("Архив расширения пуст или превышает лимит 64 МиБ", true);
    return;
  }
  const size_t off = CrxZipOffset(bytes);
  if (off >= bytes.size()) {
    ExtToast("Пакет не похож на CRX/ZIP расширения", true);
    return;
  }
  const fs::path root = ExtRootDir();
  if (root.empty() || !security::EnsureDirectoryWithoutLink(root)) {
    ExtToast("Каталог расширений недоступен или является ссылкой", true);
    return;
  }
  std::error_code ec;
  static unsigned int seq = 0;
  fs::path tmp;
  bool tmp_created = false;
  for (unsigned int attempt = 0; attempt < 100 && !tmp_created; ++attempt) {
    tmp = root / ("_tmp" + std::to_string(++seq));
    ec.clear();
    tmp_created = fs::create_directory(tmp, ec);
    if (ec) break;
  }
  if (!tmp_created || !security::IsDirectoryWithoutLink(tmp)) {
    ExtToast("Не удалось создать безопасный временный каталог", true);
    return;
  }
  const std::string zip(bytes.begin() + static_cast<std::ptrdiff_t>(off), bytes.end());
  if (!UnzipBytesTo(zip, tmp)) {
    ExtToast("Не удалось распаковать архив расширения", true);
    security::RemoveTreeWithoutFollowingLinks(root, tmp);
    return;
  }
  const fs::path manifest_path = tmp / "manifest.json";
  std::string manifest;
  if (!ReadFileStr(manifest_path, kMaxExtensionManifestBytes, &manifest) ||
      manifest.empty()) {
    ExtToast("В архиве нет допустимого manifest.json", true);
    security::RemoveTreeWithoutFollowingLinks(root, tmp);
    return;
  }
  CefRefPtr<CefValue> v = CefParseJSON(manifest, JSON_PARSER_RFC);
  std::string name, ver;
  if (v && v->GetType() == VTYPE_DICTIONARY) {
    CefRefPtr<CefDictionaryValue> d = v->GetDictionary();
    if (d->HasKey("name") && d->GetType("name") == VTYPE_STRING)
      name = d->GetString("name").ToString();
    if (d->HasKey("version") && d->GetType("version") == VTYPE_STRING)
      ver = d->GetString("version").ToString();
  }
  if (name.empty()) name = "extension";
  std::string slug = "ext-";
  for (unsigned char c : name) {
    if (slug.size() >= 60) break;
    if (c >= 'a' && c <= 'z') {
      slug.push_back(static_cast<char>(c));
    } else if (c >= 'A' && c <= 'Z') {
      slug.push_back(static_cast<char>(c - 'A' + 'a'));
    } else if (c >= '0' && c <= '9') {
      slug.push_back(static_cast<char>(c));
    } else if (slug.back() != '-') {
      slug.push_back('-');
    }
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  if (slug == "ext") slug += "-default";

  fs::path dest;
  bool destination_available = false;
  for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
    const std::string candidate =
        suffix == 0 ? slug : slug + "-" + std::to_string(suffix);
    const fs::path path = root / candidate;
    ec.clear();
    const fs::file_status status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && status.type() == fs::file_type::not_found)) {
      dest = path;
      destination_available = true;
      break;
    }
    if (ec) break;
  }
  if (!destination_available || !security::IsDirectoryWithoutLink(root) ||
      !security::IsDirectoryWithoutLink(tmp)) {
    ExtToast("Не удалось безопасно разместить расширение", true);
    security::RemoveTreeWithoutFollowingLinks(root, tmp);
    return;
  }
  ec.clear();
  fs::rename(tmp, dest, ec);
  if (ec) {
    ExtToast("Не удалось разместить расширение в профиле", true);
    security::RemoveTreeWithoutFollowingLinks(root, tmp);
    return;
  }
  LoadExtFromDir(dest);
  ExtToast("Расширение установлено: " + name + (ver.empty() ? "" : " v" + ver),
           false);
  ExtPushList();
}

// Косметическая фильтрация: прячем типовые рекламные контейнеры стилем.
// Инъекция идемпотентна (проверка по атрибуту) и ставится и на старт, и на
// конец загрузки: на старте контекст может уже существовать (SPA-переходы),
// на конце — гарантированно закрывает страницы с поздним контекстом.
static void InjectBlockerCss(CefRefPtr<CefFrame> frame) {
  if (!frame || !frame->IsValid() || !frame->IsMain()) return;
  const std::string& css = blocker::CosmeticCss();
  if (css.empty()) return;
  const std::string js =
      "(function(){if(document.querySelector('style[data-shelter-blocker]'))"
      "return;var s=document.createElement('style');"
      "s.setAttribute('data-shelter-blocker','1');s.textContent=" +
      JsString(css) +
      ";(document.head||document.documentElement).appendChild(s);})();";
  frame->ExecuteJavaScript(js, frame->GetURL(), 0);
}

// ---- анти-отпечаток ---------------------------------------------------------

void Shell::PushFpState(CefRefPtr<CefBrowser> browser) {
  if (!browser) return;
  CefRefPtr<CefProcessMessage> m = CefProcessMessage::Create("shelter.fp");
  m->GetArgumentList()->SetBool(0, fp_enabled_);
  if (auto frame = browser->GetMainFrame())
    frame->SendProcessMessage(PID_RENDERER, m);
}

void Shell::SyncProtectionFlags() {
  int flags = 0;
  if (fp_enabled_) flags |= 1;
  if (auto_consent_enabled_) flags |= 2;
  protection_flags_.store(flags, std::memory_order_relaxed);
}

void Shell::SetFpEnabled(bool on) {
  CEF_REQUIRE_UI_THREAD();
  fp_enabled_ = on;
  SyncProtectionFlags();
  for (auto& kv : tabs_) PushFpState(kv.second.browser);
}

// ---- автосогласие cookies ---------------------------------------------------

void Shell::PushCookieState(CefRefPtr<CefBrowser> browser) {
  if (!browser) return;
  CefRefPtr<CefProcessMessage> m = CefProcessMessage::Create("shelter.cookies");
  m->GetArgumentList()->SetBool(0, auto_consent_enabled_);
  if (auto frame = browser->GetMainFrame())
    frame->SendProcessMessage(PID_RENDERER, m);
}

void Shell::SetAutoConsentEnabled(bool on) {
  CEF_REQUIRE_UI_THREAD();
  auto_consent_enabled_ = on;
  SyncProtectionFlags();
  for (auto& kv : tabs_) PushCookieState(kv.second.browser);
}

// ---- HTTPS-only + DoH -------------------------------------------------------

void Shell::ApplyNetGuard() {
  CEF_REQUIRE_UI_THREAD();
  for (auto& kv : contexts_) {
    auto ready = context_ready_.find(kv.first);
    if (ready != context_ready_.end() && ready->second)
      netguard::ApplyDohPrefs(kv.second);
  }
}

void Shell::SetHttpsOnlyEnabled(bool on) {
  CEF_REQUIRE_UI_THREAD();
  netguard::SetHttpsOnlyEnabled(on);
  ApplyNetGuard();  // DoH живёт тем же тумблером, что и HTTPS-only
}

void Shell::SetDohProvider(const std::string& provider) {
  CEF_REQUIRE_UI_THREAD();
  netguard::SetDohProvider(provider);
  ApplyNetGuard();
}

// ---- строгий режим ----------------------------------------------------------

void Shell::SetStrictEnabled(bool on) {
  CEF_REQUIRE_UI_THREAD();
  netguard::SetStrictEnabled(on);
}

void Shell::StrictAllow(const std::string& host) {
  CEF_REQUIRE_UI_THREAD();
  netguard::AllowScriptsFor(host);
}

// ---- призрак ------------------------------------------------------------------

void Shell::SetGhostMode(bool on) {
  CEF_REQUIRE_UI_THREAD();
  if (on) return;  // при включении профили создаются лениво по запросу вкладок
  // При выключении сразу освобождаем in-memory контексты без вкладок, чтобы
  // приватная сессия исчезла из памяти как можно раньше.
  std::vector<std::string> temp_parts;
  for (auto& kv : contexts_) {
    if (kv.first.rfind("temp:", 0) == 0) temp_parts.push_back(kv.first);
  }
  for (auto& p : temp_parts) ReleaseContextIfUnused(p);
}

// ---- разрешения сайтов и иконки ---------------------------------------------

void Shell::NotePermissionRequest(const std::string& origin, uint32_t mask,
                                  bool media) {
  CEF_REQUIRE_UI_THREAD();
  // Никаких записей на диск: событие живёт только в UI. Тост показывает, кто
  // и что просил, — «тихий» отказ рантайма выглядел бы как сломанная страница.
  std::string host;
  CefURLParts parts;
  if (CefParseURL(origin, parts)) host = CefString(&parts.host).ToString();
  std::ostringstream os;
  os << "{\"origin\":" << JsString(origin) << ",\"host\":" << JsString(host)
     << ",\"mask\":" << mask << ",\"media\":" << (media ? "1" : "0") << "}";
  UiEvent("perm", os.str());
}

bool Shell::FaviconNeeded(const std::string& tab_id, const std::string& host) {
  CEF_REQUIRE_UI_THREAD();
  if (host.empty() || favicon_hosts_.size() >= 400) return false;
  Tab* t = FindTab(tab_id);
  if (t && IsGhostPartition(t->partition)) return false;  // призрак не кэширует
  if (!favicon_hosts_.insert(host).second) return false;
  return true;
}

void Shell::FaviconRequestStarted(const std::string& url,
                                  CefRefPtr<CefURLRequest> request) {
  CEF_REQUIRE_UI_THREAD();
  favicon_requests_[url] = request;
}

void Shell::OnFaviconReady(const std::string& tab_id, const std::string& host,
                           const std::string& url, const std::string& data) {
  CEF_REQUIRE_UI_THREAD();
  favicon_requests_.erase(url);
  Tab* t = FindTab(tab_id);
  if (t && IsGhostPartition(t->partition)) return;
  if (data.empty() || data.size() > 400000) return;
  std::ostringstream os;
  os << "{\"tabId\":" << JsString(tab_id) << ",\"host\":" << JsString(host)
     << ",\"data\":" << JsString(data) << "}";
  UiEvent("fav", os.str());
}

void Shell::OnTabLoadStart(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame) {
  CEF_REQUIRE_UI_THREAD();
  InjectExtScripts(browser, frame, true);
  if (blocker::Enabled()) InjectBlockerCss(frame);
}

void Shell::OnTabLoadEnd(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame) {
  CEF_REQUIRE_UI_THREAD();
  InjectExtScripts(browser, frame, false);
  if (blocker::Enabled()) InjectBlockerCss(frame);
}

void Shell::OnTabDownloadUpdated(
    CefRefPtr<CefDownloadItem> item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!item || !item->IsValid()) return;

  const std::string id = "dl" + std::to_string(item->GetId());
  auto accepted = accepted_downloads_.find(id);
  // CEF may send an update before OnBeforeDownload or while the confirmation
  // dialog is still open. Do not show an unapproved transfer in the UI.
  if (accepted == accepted_downloads_.end()) return;

  const bool complete = item->IsComplete();
  const bool canceled = item->IsCanceled();
  const bool interrupted = item->IsInterrupted();
  const bool paused = item->IsPaused();
  std::string state = "progressing";
  if (complete) {
    state = "completed";
  } else if (canceled) {
    state = "cancelled";
  } else if (interrupted) {
    state = "interrupted";
  } else if (paused) {
    state = "paused";
  } else if (!item->IsInProgress()) {
    state = "interrupted";
  }
  const bool terminal = complete || canceled || interrupted ||
                        (!paused && !item->IsInProgress());

  if (state == "progressing" || state == "paused") {
    if (callback) active_downloads_[id] = {callback};
  }

  const std::int64_t reported_bytes = item->GetReceivedBytes();
  const std::int64_t bytes = reported_bytes > 0 ? reported_bytes : 0;
  const std::int64_t reported_total = item->GetTotalBytes();
  const std::int64_t total = reported_total > 0 ? reported_total : 0;
  const std::int64_t reported_speed = item->GetCurrentSpeed();
  const std::int64_t speed = reported_speed > 0 ? reported_speed : 0;
  int percent = complete ? 100 : item->GetPercentComplete();
  if (percent < 0 && total > 0) {
    const long double ratio = static_cast<long double>(bytes) /
                              static_cast<long double>(total);
    percent = static_cast<int>(std::min<long double>(99, ratio * 100.0L));
  } else if (!complete && percent >= 100) {
    // CEF's percent is intentionally rough; reserve 100% for IsComplete().
    percent = 99;
  }
  std::string path = item->GetFullPath().ToString();
  auto temp = temp_downloads_.find(id);
  if (temp != temp_downloads_.end()) {
    if (state == "completed") {
      // Загрузка полностью завершена: «не подтверждённый» временный файл
      // становится итоговым (как в современных браузерах).
      std::error_code rec;
      fs::rename(fs::u8path(temp->second.first), fs::u8path(temp->second.second), rec);
      const bool dl_priv = private_downloads_.count(id) > 0;
      if (!rec) {
        path = temp->second.second;
        Log("download finalized id=" + id +
            (dl_priv ? " private=1" : " ext=" + ExtLabel(path)));
      } else {
        Log("download rename failed id=" + id + " error=" + rec.message());
      }
    } else if (state == "cancelled" || state == "interrupted") {
      // Прерванная или отменённая загрузка не оставляет «не подтверждённый» файл.
      std::error_code rec;
      fs::remove(fs::u8path(temp->second.first), rec);
    }
  }
  std::string name = item->GetSuggestedFileName().ToString();
  if (name.empty() && !path.empty())
    name = PathToUtf8(fs::u8path(path).filename());
  if (name.empty()) name = accepted->second.empty() ? "download" : accepted->second;

  const bool dl_priv = private_downloads_.count(id) > 0;
  if (state != "progressing")
    Log("download update id=" + id + " state=" + state + " bytes=" +
        std::to_string(bytes) + " total=" + std::to_string(total) +
        " reason=" + std::to_string(static_cast<int>(item->GetInterruptReason())) +
        (dl_priv ? " private=1" : " ext=" + ExtLabel(path)));

  std::ostringstream os;
  os << "{\"id\":" << JsString(id) << ",\"filename\":" << JsString(name)
     << ",\"state\":" << JsString(state)
     << ",\"bytes\":" << bytes
     << ",\"total\":" << total
     << ",\"percent\":" << percent
     << ",\"speed\":" << speed
     << ",\"url\":" << JsString(item->GetURL().ToString())
     << ",\"path\":" << JsString(path) << "}";
  UiEvent("download", os.str());

  if (terminal) {
    accepted_downloads_.erase(id);
    active_downloads_.erase(id);
    temp_downloads_.erase(id);
    private_downloads_.erase(id);  // приватность записи больше не нужна
  }
}

}  // namespace shelter
