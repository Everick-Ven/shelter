#include "src/ui_scheme.h"

#include <fstream>
#include <sstream>
#include <string>

#include "include/cef_parser.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "src/common.h"
#include "src/platform.h"

namespace shelter {

namespace {

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

void ReplaceAll(std::string* s, const std::string& from, const std::string& to) {
  size_t pos = 0;
  while ((pos = s->find(from, pos)) != std::string::npos) {
    s->replace(pos, from.size(), to);
    pos += to.size();
  }
}

// Безопасный относительный путь внутри каталога UI.
bool SanitizeRelPath(std::string* rel) {
  if (rel->empty() || *rel == "/") *rel = "index.html";
  if ((*rel)[0] == '/') rel->erase(0, 1);
  if (rel->find("..") != std::string::npos) return false;
  for (char c : *rel) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
              c == '/';
    if (!ok) return false;
  }
  return true;
}

// Мост подключается в index.html без правки самого файла: тег <script>
// вставляется перед самым первым <script>, то есть до кода UI (но после CSP <meta>).
std::string InjectBridge(std::string html) {
  const std::string tag = "<script src=\"host-bridge.js\"></script>\n";
  size_t pos = html.find("<script");
  if (pos == std::string::npos) pos = html.find("</head>");
  if (pos == std::string::npos) return tag + html;
  html.insert(pos, tag);
  return html;
}

// Небольшой «читающий» хук состояния (space/ghost по id вкладки). Вставляется при отдаче
// страницы — файл дизайна на диске остаётся нетронутым. Если маркер не найден (дизайн
// обновили), host-bridge.js работает в упрощённом режиме.
void InjectStateHook(std::string* html) {
  const std::string marker = "window.getActiveTabId = () => S.activeTabId;";
  size_t pos = html->find(marker);
  if (pos == std::string::npos) return;
  const std::string hook =
      "\nwindow.__shelterTab = id => { const ks = Object.keys(S.spaces || {});"
      " for (const k of ks) { const t = (S.spaces[k].tabs || []).find(x => x.id === id);"
      " if (t) return { space: k, ghost: !!(t.ghost || S.ghost) }; }"
      " return { space: S.currentSpace, ghost: !!S.ghost }; };";
  html->insert(pos + marker.size(), hook);
}

// Если index.html не найден рядом с exe (запуск прямо из архива без распаковки,
// потерянная папка ui/ при копировании), вместо пустого чёрного окна отдаём
// встроенную страницу-подсказку: пользователю видно, что произошло и что делать.
std::string MissingUiHintHtml(const std::string& looked_in) {
  std::string html = R"(<!doctype html><html lang="ru"><head><meta charset="utf-8">
<title>SHELTER — неполная папка</title><style>
html,body{height:100%;margin:0;background:#0E0E10;color:#EDEDF0;
font:15px/1.6 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
display:flex;align-items:center;justify-content:center}
.card{max-width:560px;padding:36px 40px;border-radius:20px;background:#141417;
border:1px solid #26262B;box-shadow:0 24px 80px rgba(0,0,0,.5)}
.logo{width:52px;height:52px;border-radius:14px;background:#000;color:#fff;
display:flex;align-items:center;justify-content:center;
font:700 26px/1 system-ui,sans-serif;margin-bottom:18px;border:1px solid #2E2E33}
h1{font-size:20px;margin:0 0 10px}p{margin:0 0 10px;color:#B9B9C0}
code{background:#1D1D22;border:1px solid #2A2A30;border-radius:6px;
padding:1px 6px;color:#E8E8EC;font-size:13px}
ol{margin:0 0 12px;padding-left:20px;color:#B9B9C0}li{margin:4px 0}
b{color:#EDEDF0}</style></head><body><div class="card">
<div class="logo">S</div>
<h1>Папка приложения неполная</h1>
<p>SHELTER запустился, но не нашёл рядом свои файлы интерфейса — поэтому
окно пустое. Обычно это значит, что <b>exe запустили прямо из архива</b>
или скопировали без папки <code>ui</code>.</p>
<ol>
<li>Распакуйте <b>всю папку</b> архива (например <code>SHELTER-win-x64</code>),
а не только exe;</li>
<li>запустите <code>SHELTER.exe</code> из распакованной папки —
либо установите приложение через <code>SHELTER-Setup-x64.exe</code>.</li>
</ol>
<p>Искали интерфейс здесь: <code>@DIR@</code></p>
</div></body></html>)";
  ReplaceAll(&html, "@DIR@", looked_in);
  return html;
}

class UiSchemeHandlerFactory : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    CEF_REQUIRE_IO_THREAD();

    // UI-страница доступна только из нашего UI-браузера и никогда — из вкладок.
    const std::string url = request->GetURL().ToString();
    std::string rel = url.substr(std::string(kUiOriginPrefix).size() - 1);
    size_t cut = rel.find_first_of("?#");
    if (cut != std::string::npos) rel.resize(cut);
    if (!SanitizeRelPath(&rel)) return NotFound();

    std::string data;
    if (!ReadFile(platform::UiResourceDir() + "/" + rel, &data)) {
      if (rel == "index.html") {
        // Чёрное пустое окно пугает и не объясняет причину. Отдаём подсказку.
        std::string hint = MissingUiHintHtml(platform::UiResourceDir());
        CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForData(
            hint.data(), hint.size());
        CefResponse::HeaderMap headers;
        headers.insert({"Cache-Control", "no-store"});
        return new CefStreamResourceHandler(200, "OK", "text/html", headers,
                                            stream);
      }
      return NotFound();
    }

    if (rel == "index.html") {
      InjectStateHook(&data);
      data = InjectBridge(std::move(data));
    } else if (rel == "host-bridge.js") {
      ReplaceAll(&data, "__PLATFORM__", kPlatform);
      ReplaceAll(&data, "__APP_VERSION__", kAppVersion);
      ReplaceAll(&data, "__SECRET_KEY__", platform::SecretKeyHex());
    }

    std::string ext;
    size_t dot = rel.find_last_of('.');
    if (dot != std::string::npos) ext = rel.substr(dot + 1);
    std::string mime = CefGetMimeType(ext);
    if (mime.empty()) mime = "application/octet-stream";

    CefRefPtr<CefStreamReader> stream =
        CefStreamReader::CreateForData(data.data(), data.size());
    CefResponse::HeaderMap headers;
    headers.insert({"Cache-Control", "no-store"});
    return new CefStreamResourceHandler(200, "OK", mime, headers, stream);
  }

 private:
  static CefRefPtr<CefResourceHandler> NotFound() {
    // Пустое тело: отсутствующий необязательный скрипт (native.js и т.п.) не должен
    // исполняться как «Not found».
    static const char kEmpty[] = " ";
    CefRefPtr<CefStreamReader> stream =
        CefStreamReader::CreateForData(const_cast<char*>(kEmpty), 1);
    return new CefStreamResourceHandler(404, "Not Found", "text/plain",
                                        CefResponse::HeaderMap(), stream);
  }

  IMPLEMENT_REFCOUNTING(UiSchemeHandlerFactory);
};

}  // namespace

void RegisterUiScheme() {
  CefRegisterSchemeHandlerFactory(kUiScheme, kUiHost,
                                  new UiSchemeHandlerFactory());
}

}  // namespace shelter
