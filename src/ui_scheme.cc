#include "src/ui_scheme.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "include/cef_parser.h"
#include "include/cef_post_data.h"
#include "include/cef_post_data_element.h"
#include "include/cef_request.h"
#include "include/cef_scheme.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "src/common.h"
#include "src/platform.h"
#include "src/secret_crypto.h"
#include "src/shell.h"

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

constexpr size_t kMaxSecretRequestBytes = 64u * 1024u * 1024u;

bool ReadPostBody(CefRefPtr<CefRequest> request, std::string* body) {
  if (!request || !body) return false;
  CefRefPtr<CefPostData> post = request->GetPostData();
  if (!post) return false;
  std::vector<CefRefPtr<CefPostDataElement>> elements;
  post->GetElements(elements);
  size_t total = 0;
  for (const auto& element : elements) {
    if (!element || element->GetType() != PDE_TYPE_BYTES) return false;
    const size_t count = element->GetBytesCount();
    if (count > kMaxSecretRequestBytes - total) return false;
    total += count;
  }
  body->assign(total, '\0');
  size_t offset = 0;
  for (const auto& element : elements) {
    const size_t count = element->GetBytesCount();
    if (count && element->GetBytes(count, body->data() + offset) != count) {
      body->clear();
      return false;
    }
    offset += count;
  }
  return true;
}

CefRefPtr<CefResourceHandler> SecretResponse(bool ok,
                                            const std::string& value) {
  CefRefPtr<CefDictionaryValue> dict = CefDictionaryValue::Create();
  dict->SetBool("ok", ok);
  if (ok) dict->SetString("value", value);
  CefRefPtr<CefValue> json = CefValue::Create();
  json->SetDictionary(dict);
  const std::string data = CefWriteJSON(json, JSON_WRITER_DEFAULT).ToString();
  CefRefPtr<CefStreamReader> stream =
      CefStreamReader::CreateForData(data.data(), data.size());
  CefResponse::HeaderMap headers;
  headers.insert({"Cache-Control", "no-store"});
  headers.insert({"X-Content-Type-Options", "nosniff"});
  return new CefStreamResourceHandler(200, "OK", "application/json", headers,
                                      stream);
}

CefRefPtr<CefResourceHandler> HandleSecretRequest(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, const std::string& operation) {
  if (!browser || !frame || !frame->IsMain() || !request ||
      !Shell::Get().IsUiBrowserId(browser->GetIdentifier()) ||
      request->GetMethod().ToString() != "POST") {
    return SecretResponse(false, std::string());
  }
  std::string input, output;
  const bool parsed = ReadPostBody(request, &input);
  const bool ok = parsed &&
      (operation == "encrypt"
           ? shelter::secret_crypto::Encrypt(input, &output)
           : shelter::secret_crypto::Decrypt(input, &output));
  std::fill(input.begin(), input.end(), '\0');
  CefRefPtr<CefResourceHandler> response = SecretResponse(ok, ok ? output : "");
  std::fill(output.begin(), output.end(), '\0');
  return response;
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

    // The bridge exposes privileged native UI capabilities and app resources.
    // Do not serve it to web tabs, popups, or subframes, even on this scheme.
    if (!browser || !frame || !frame->IsMain() || !request ||
        !Shell::Get().IsUiBrowserId(browser->GetIdentifier())) {
      return NotFound();
    }
    const std::string url = request->GetURL().ToString();
    if (url.rfind(kUiOriginPrefix, 0) != 0) return NotFound();
    std::string rel = url.substr(std::string(kUiOriginPrefix).size() - 1);
    size_t cut = rel.find_first_of("?#");
    if (cut != std::string::npos) rel.resize(cut);
    if (!SanitizeRelPath(&rel)) return NotFound();

    if (rel == "secret/encrypt" || rel == "secret/decrypt") {
      return HandleSecretRequest(browser, frame, request,
                                 rel == "secret/encrypt" ? "encrypt" : "decrypt");
    }

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
      data = InjectBridge(std::move(data));
    } else if (rel == "host-bridge.js") {
      ReplaceAll(&data, "__PLATFORM__", kPlatform);
      ReplaceAll(&data, "__APP_VERSION__", kAppVersion);
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
