// SHELTER — мост UI -> native (window.shelterNative / window.shelter* хуки из host-bridge.js).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <utility>
#if defined(SHELTER_CI_HOOKS)
#include <chrono>
#include <cstdint>
#include <iomanip>
#endif

#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_task.h"
#include "include/cef_thread.h"
#include "include/cef_urlrequest.h"
#include "include/wrapper/cef_helpers.h"
#include "src/blocker.h"
#include "src/clients.h"
#include "src/color_scheme.h"
#include "src/common.h"
#include "src/path_utils.h"
#include "src/platform.h"
#include "src/security_paths.h"
#include "src/shell.h"

namespace shelter {

namespace {

using Callback = CefMessageRouterBrowserSide::Callback;

class CloseAfterUserDataDeleteTask : public CefTask {
 public:
  void Execute() override { Shell::Get().RequestClose(); }

 private:
  IMPLEMENT_REFCOUNTING(CloseAfterUserDataDeleteTask);
};

double Num(CefRefPtr<CefDictionaryValue> d, const char* key, double def = 0) {
  if (!d || !d->HasKey(key)) return def;
  switch (d->GetType(key)) {
    case VTYPE_INT: return d->GetInt(key);
    case VTYPE_DOUBLE: return d->GetDouble(key);
    case VTYPE_BOOL: return d->GetBool(key) ? 1 : 0;
    default: return def;
  }
}

bool Flag(CefRefPtr<CefDictionaryValue> d, const char* key, bool def) {
  if (!d || !d->HasKey(key)) return def;
  return d->GetType(key) == VTYPE_BOOL ? d->GetBool(key) : Num(d, key) != 0;
}

std::string Str(CefRefPtr<CefDictionaryValue> d, const char* key) {
  if (!d || !d->HasKey(key) || d->GetType(key) != VTYPE_STRING) return "";
  return d->GetString(key).ToString();
}

CefRect RectOf(CefRefPtr<CefDictionaryValue> args) {
  if (!args->HasKey("rect") || args->GetType("rect") != VTYPE_DICTIONARY) {
    return CefRect();
  }
  CefRefPtr<CefDictionaryValue> r = args->GetDictionary("rect");
  return CefRect(static_cast<int>(std::lround(Num(r, "x"))),
                 static_cast<int>(std::lround(Num(r, "y"))),
                 static_cast<int>(std::lround(Num(r, "w"))),
                 static_cast<int>(std::lround(Num(r, "h"))));
}

// Во вкладки передаём только HTTP(S) или about:blank. Файл, выбранный в
// системном диалоге, проходит отдельный одноразовый путь ниже.
std::string NavUrl(std::string url) {
  const auto begin = url.find_first_not_of(" \t\r\n");
  const auto end = url.find_last_not_of(" \t\r\n");
  url = begin == std::string::npos ? "" : url.substr(begin, end - begin + 1);
  if (url.empty()) return "about:blank";
  for (unsigned char c : url) {
    if (c <= 0x20 || c == 0x7f) return "about:blank";
  }

  std::string lower = url;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "about:blank") return "about:blank";

  const size_t colon = lower.find(':');
  const size_t delimiter = lower.find_first_of("/?#");
  const bool has_scheme = colon != std::string::npos && colon > 0 &&
                          (delimiter == std::string::npos || colon < delimiter) &&
                          lower[0] >= 'a' && lower[0] <= 'z' &&
                          std::all_of(lower.begin(), lower.begin() + colon,
                                      [](unsigned char c) {
                                        return (c >= 'a' && c <= 'z') ||
                                               (c >= '0' && c <= '9') ||
                                               c == '+' || c == '-' || c == '.';
                                      });
  std::string candidate = url;
  if (has_scheme) {
    const std::string scheme = lower.substr(0, colon);
    if (scheme != "http" && scheme != "https") return "about:blank";
  } else {
    candidate = "https://" + url;
  }

  CefURLParts parts;
  if (!CefParseURL(CefString(candidate), parts)) return "about:blank";
  std::string scheme = CefString(&parts.scheme).ToString();
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if ((scheme != "http" && scheme != "https") ||
      CefString(&parts.host).ToString().empty()) {
    return "about:blank";
  }
  return candidate;
}

// ---- снимок вкладки через DevTools-протокол --------------------------------

class SnapObserver : public CefDevToolsMessageObserver {
 public:
  void Add(int message_id, CefRefPtr<Callback> cb) { pending_[message_id] = cb; }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message_id,
                              bool success, const void* result,
                              size_t result_size) override {
    auto it = pending_.find(message_id);
    if (it == pending_.end()) return;
    CefRefPtr<Callback> cb = it->second;
    pending_.erase(it);
    if (!success) {
      cb->Failure(500, "capture failed");
      return;
    }
    CefRefPtr<CefValue> v = CefParseJSON(result, result_size, JSON_PARSER_RFC);
    if (v && v->GetType() == VTYPE_DICTIONARY &&
        v->GetDictionary()->HasKey("data")) {
      cb->Success("data:image/jpeg;base64," +
                  v->GetDictionary()->GetString("data").ToString());
    } else {
      cb->Failure(500, "no data");
    }
  }

 private:
  std::map<int, CefRefPtr<Callback>> pending_;
  IMPLEMENT_REFCOUNTING(SnapObserver);
};

// ---- очистка cookie с фильтром по времени ----------------------------------

class CookieClearVisitor : public CefCookieVisitor {
 public:
  explicit CookieClearVisitor(double begin_ms) : begin_ms_(begin_ms) {}
  bool Visit(const CefCookie& cookie, int, int, bool& deleteCookie) override {
    if (begin_ms_ <= 0) {
      deleteCookie = true;
    } else {
      const double created_ms =
          static_cast<double>(cookie.creation.val) / 1000.0 - 11644473600000.0;
      deleteCookie = created_ms >= begin_ms_;
    }
    return true;
  }

 private:
  double begin_ms_;
  IMPLEMENT_REFCOUNTING(CookieClearVisitor);
};

#if defined(SHELTER_CI_HOOKS)
using BridgePerfClock = std::chrono::steady_clock;

struct BridgePerfBucket {
  std::uint64_t count = 0;
  double total_ms = 0;
  double max_ms = 0;
  BridgePerfClock::time_point last_report{};
};

std::map<std::string, BridgePerfBucket>& BridgePerfBuckets() {
  static std::map<std::string, BridgePerfBucket> buckets;
  return buckets;
}

const std::set<std::string>& DiagnosticMethodNames() {
  static const std::set<std::string> names = {
      "premium.status", "premium.checkout", "user-data.delete",
      "win.state", "win.minimize", "win.maximize", "win.close", "win.fullscreen",
      "favicon.ensure", "ext.list", "ext.setMode", "ext.install",
      "ext.remove", "ext.pick", "file.open", "win.openPlain",
      "view.open", "view.layout", "view.clip", "view.hide", "view.show",
      "tabs.sync", "view.act", "ctx.act", "view.snap", "view.zoom",
      "find", "devtools", "clip.read", "clip.write", "theme.scheme",
      "shell.show", "block.setEnabled", "fp.setEnabled",
      "cookie.setEnabled", "https.setEnabled", "dns.setProvider",
      "strict.setEnabled", "strict.allow", "ghost.setEnabled",
      "shell.filestate", "dl.decision", "dl.control", "privacy.clear",
      "dbg.protections", "dbg.hit", "dbg.clip", "dbg.window",
      "window.minimize",
      "window.maximize", "window.close", "window.openPlain",
      "privacy.clear", "auth.open", "devtools.ctl", "ai.test", "ai.chat",
      "clipboard.read", "clipboard.write", "tab.captureThumbs",
      "tab.hideContent", "tab.navigate", "tab.reload", "tab.stop",
      "tab.zoom", "ui.ready", "viewport.sync", "diag.performance",
      "ui.longtask", "freeze.snapshot", "freeze.decode", "freeze.hide",
      "freeze.total", "thaw.show", "thaw.total", "perm.allow",
      "cert.decision", "app.checkUpdates", "other"};
  return names;
}

std::string DiagnosticMethod(const std::string& name) {
  return DiagnosticMethodNames().count(name) ? name : "other";
}

double BoundedPerfValue(double value, double maximum) {
  return std::isfinite(value) ? std::clamp(value, 0.0, maximum) : 0.0;
}

std::string PerfMilliseconds(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << BoundedPerfValue(value, 1000000.0);
  return out.str();
}

template <typename Callback>
class ScopeExit {
 public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;

 private:
  Callback callback_;
};
#endif  // SHELTER_CI_HOOKS

// ---- внешний ИИ-агент (OpenAI-совместимый API) ------------------------------
// Ключ и HTTPS-трафик живут только в браузерном процессе: страница отдаёт
// конфиг через мост, натив выполняет запрос и возвращает лишь текст
// ответа/ошибку. CSP страницы не трогаем — connect-src остаётся строгим.

// Лимиты ответов провайдера: модели отдают текст, а не файлы.
constexpr size_t kAiTestMaxBytes = 128 * 1024;
constexpr size_t kAiChatMaxBytes = 2 * 1024 * 1024;

bool IsLoopbackHost(const std::string& host) {
  std::string lower = host;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (lower.size() >= 2 && lower.front() == '[' && lower.back() == ']')
    lower = lower.substr(1, lower.size() - 2);
  return lower == "localhost" || lower == "127.0.0.1" || lower == "::1";
}

// Собирает URL вида "<base>/<path>": наружу — только https; http — лишь на
// loopback (локальные Ollama/LM Studio). Пустая строка — недопустимый baseUrl.
std::string AiApiUrl(const std::string& base, const char* path) {
  const auto begin = base.find_first_not_of(" \t\r\n");
  const auto end = base.find_last_not_of(" \t\r\n");
  std::string clean =
      begin == std::string::npos ? "" : base.substr(begin, end - begin + 1);
  while (!clean.empty() && clean.back() == '/') clean.pop_back();
  if (clean.empty()) return "";
  for (unsigned char c : clean) {
    if (c <= 0x20 || c == 0x7f) return "";
  }
  // Без userinfo: «https://провайдер@чужой-хост/» уведёт ключ не туда.
  const size_t scheme_end = clean.find("://");
  const size_t auth_begin =
      scheme_end == std::string::npos ? 0 : scheme_end + 3;
  const size_t auth_end = clean.find_first_of("/?#", auth_begin);
  const std::string authority = clean.substr(
      auth_begin, auth_end == std::string::npos ? auth_end
                                               : auth_end - auth_begin);
  if (authority.find('@') != std::string::npos) return "";
  CefURLParts parts;
  if (!CefParseURL(CefString(clean), parts)) return "";
  std::string scheme = CefString(&parts.scheme).ToString();
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (scheme != "https" && scheme != "http") return "";
  const std::string host = CefString(&parts.host).ToString();
  if (host.empty()) return "";
  if (scheme == "http" && !IsLoopbackHost(host)) return "";
  return clean + "/" + path;
}

// Однострочный фрагмент тела ответа для диагностики: провайдер обычно кладёт
// сюда причину 4xx. Управляющие символы вырезаем, длину ограничиваем.
std::string AiErrorSnippet(const std::string& body, size_t max_len = 300) {
  std::string out;
  out.reserve(std::min(body.size(), max_len));
  for (unsigned char c : body) {
    if (out.size() >= max_len) break;
    if (c < 0x20 || c == 0x7f) {
      if (c == '\t') out += ' ';
      continue;
    }
    out += static_cast<char>(c);
  }
  const auto end = out.find_last_not_of(' ');
  return end == std::string::npos ? "" : out.substr(0, end + 1);
}

std::string AiNetError(CefURLRequest::ErrorCode code) {
  switch (code) {
    case ERR_INTERNET_DISCONNECTED:
      return "offline";
    case ERR_NAME_NOT_RESOLVED:
      return "dns_error";
    case ERR_CONNECTION_REFUSED:
      return "connection_refused";
    case ERR_TIMED_OUT:
    case ERR_CONNECTION_TIMED_OUT:
      return "timeout";
    default:
      return "network_error";
  }
}

// choices[0].message.content из OpenAI-совместимого ответа; "" — чужой формат.
std::string AiChatText(const std::string& body) {
  CefRefPtr<CefValue> v = ParseJson(body);
  if (!v || v->GetType() != VTYPE_DICTIONARY) return "";
  CefRefPtr<CefDictionaryValue> root = v->GetDictionary();
  if (!root || !root->HasKey("choices") ||
      root->GetType("choices") != VTYPE_LIST) {
    return "";
  }
  CefRefPtr<CefListValue> choices = root->GetList("choices");
  if (!choices || choices->GetSize() == 0 ||
      choices->GetType(0) != VTYPE_DICTIONARY) {
    return "";
  }
  CefRefPtr<CefDictionaryValue> first = choices->GetDictionary(0);
  if (!first || !first->HasKey("message") ||
      first->GetType("message") != VTYPE_DICTIONARY) {
    return "";
  }
  CefRefPtr<CefDictionaryValue> message = first->GetDictionary("message");
  if (!message || !message->HasKey("content") ||
      message->GetType("content") != VTYPE_STRING) {
    return "";
  }
  return message->GetString("content").ToString();
}

// true, если провайдер отдал разборчивый список моделей, а нужной в нём нет
// (почти наверняка опечатка в имени). Неразборчивый список — не повод
// отказывать: ключ уже проверен кодом 200.
bool AiModelsListExcludes(const std::string& body, const std::string& model,
                          std::string* error) {
  if (model.empty() || !error) return false;
  CefRefPtr<CefValue> v = ParseJson(body);
  if (!v || v->GetType() != VTYPE_DICTIONARY) return false;
  CefRefPtr<CefDictionaryValue> root = v->GetDictionary();
  if (!root || !root->HasKey("data") || root->GetType("data") != VTYPE_LIST)
    return false;
  CefRefPtr<CefListValue> data = root->GetList("data");
  if (!data) return false;
  for (size_t i = 0; i < data->GetSize(); ++i) {
    if (data->GetType(i) != VTYPE_DICTIONARY) continue;
    CefRefPtr<CefDictionaryValue> item = data->GetDictionary(i);
    if (item && item->HasKey("id") && item->GetType("id") == VTYPE_STRING &&
        item->GetString("id").ToString() == model) {
      return false;
    }
  }
  *error = "unknown_model: " + model;
  return true;
}

// Один запрос к ИИ-провайдеру: держит мост-колбэк до завершения и отдаёт
// вердикт в UI. Создаётся на UI-потоке — колбэки CEF приходят туда же, так
// что мост-колбэк вызываем напрямую. Запрос удерживаем сами (request_):
// на внутреннее самоудержание CefURLRequest не полагаемся.
class AiUrlRequestClient : public CefURLRequestClient {
 public:
  enum class Mode { kTest, kChat };

  AiUrlRequestClient(Mode mode, std::string model, size_t max_bytes,
                     CefRefPtr<Callback> cb)
      : mode_(mode),
        model_(std::move(model)),
        max_bytes_(max_bytes),
        cb_(std::move(cb)) {}

  void Bind(CefRefPtr<CefURLRequest> request) {
    request_ = std::move(request);
  }

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    const auto status = request->GetRequestStatus();
    if (status != UR_SUCCESS) {
      const bool too_large =
          status == UR_CANCELED && bytes_.size() >= max_bytes_;
      Fail(too_large ? "response_too_large"
                     : (status == UR_CANCELED
                            ? "cancelled"
                            : AiNetError(request->GetRequestError())));
    } else {
      CefRefPtr<CefResponse> r = request->GetResponse();
      const int http_code = r ? r->GetStatus() : 0;
      if (http_code != 200) {
        std::string err = "http_" + std::to_string(http_code);
        const std::string snippet = AiErrorSnippet(bytes_);
        if (!snippet.empty()) err += ": " + snippet;
        Fail(err);
      } else if (mode_ == Mode::kChat) {
        const std::string text = AiChatText(bytes_);
        if (text.empty()) {
          Fail("bad_response");
        } else {
          cb_->Success(std::string("{\"ok\":true,\"text\":") + JsString(text) +
                       "}");
        }
      } else {
        std::string model_error;
        if (AiModelsListExcludes(bytes_, model_, &model_error)) {
          Fail(model_error);
        } else {
          cb_->Success("{\"ok\":true}");
        }
      }
    }
    request_ = nullptr;
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request, int64_t current,
                          int64_t) override {
    if (current > static_cast<int64_t>(max_bytes_)) request->Cancel();
  }
  void OnDownloadData(CefRefPtr<CefURLRequest>, const void* data,
                      size_t data_length) override {
    if (bytes_.size() >= max_bytes_) return;
    bytes_.append(static_cast<const char*>(data),
                  std::min(data_length, max_bytes_ - bytes_.size()));
  }
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&,
                          const CefString&,
                          CefRefPtr<CefAuthCallback>) override {
    return false;
  }

 private:
  void Fail(const std::string& error) {
    // Именно Success: host-bridge на onFailure отдаёт null, и текст причины
    // потерялся бы, а диалог показывает r.error как есть.
    cb_->Success(std::string("{\"ok\":false,\"error\":") + JsString(error) +
                 "}");
  }

  Mode mode_;
  std::string model_;
  size_t max_bytes_;
  CefRefPtr<Callback> cb_;
  CefRefPtr<CefURLRequest> request_;
  std::string bytes_;
  IMPLEMENT_REFCOUNTING(AiUrlRequestClient);
  DISALLOW_COPY_AND_ASSIGN(AiUrlRequestClient);
};

// Общий запуск ai.test/ai.chat: валидация, сборка запроса, возврат true —
// вердикт придёт в мост-колбэк асинхронно.
bool StartAiRequest(CefRefPtr<CefBrowser> browser, bool chat,
                    CefRefPtr<CefDictionaryValue> a, CefRefPtr<Callback> cb) {
  CefRefPtr<CefDictionaryValue> cfg = a;
  std::string prompt;
  if (chat) {
    if (!a || !a->HasKey("cfg") || a->GetType("cfg") != VTYPE_DICTIONARY) {
      cb->Success("{\"ok\":false,\"error\":\"missing_config\"}");
      return true;
    }
    cfg = a->GetDictionary("cfg");
    prompt = Str(a, "prompt");
    if (prompt.empty()) {
      cb->Success("{\"ok\":false,\"error\":\"empty_prompt\"}");
      return true;
    }
  }
  const std::string model = Str(cfg, "model");
  const std::string url =
      AiApiUrl(Str(cfg, "baseUrl"), chat ? "chat/completions" : "models");
  if (url.empty()) {
    cb->Success("{\"ok\":false,\"error\":\"invalid_url\"}");
    return true;
  }
  const std::string api_key = Str(cfg, "apiKey");
  if (api_key.empty()) {
    cb->Success("{\"ok\":false,\"error\":\"missing_api_key\"}");
    return true;
  }
  CefRefPtr<CefRequest> req = CefRequest::Create();
  req->SetURL(url);
  CefRequest::HeaderMap headers;
  headers.insert(std::make_pair(CefString("Authorization"),
                                CefString("Bearer " + api_key)));
  if (chat) {
    req->SetMethod("POST");
    headers.insert(std::make_pair(CefString("Content-Type"),
                                  CefString("application/json")));
    CefRefPtr<CefDictionaryValue> body = CefDictionaryValue::Create();
    body->SetString("model", model);
    CefRefPtr<CefDictionaryValue> msg = CefDictionaryValue::Create();
    msg->SetString("role", "user");
    msg->SetString("content", prompt);
    CefRefPtr<CefListValue> messages = CefListValue::Create();
    messages->SetDictionary(0, msg);
    body->SetList("messages", messages);
    body->SetInt("max_tokens", 1024);
    const std::string payload = ToJson(body);
    CefRefPtr<CefPostData> post = CefPostData::Create();
    CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
    element->SetToBytes(payload.size(), payload.c_str());
    post->AddElement(element);
    req->SetPostData(post);
  } else {
    req->SetMethod("GET");
  }
  req->SetHeaderMap(headers);
  CefRefPtr<AiUrlRequestClient> client = new AiUrlRequestClient(
      chat ? AiUrlRequestClient::Mode::kChat : AiUrlRequestClient::Mode::kTest,
      model, chat ? kAiChatMaxBytes : kAiTestMaxBytes, cb);
  client->Bind(CefURLRequest::Create(
      req, client, browser->GetHost()->GetRequestContext()));
  return true;
}

}  // namespace

// ============================================================================

bool Shell::HandleBridge(CefRefPtr<CefBrowser> browser, const std::string& m,
                         CefRefPtr<CefDictionaryValue> a,
                         CefRefPtr<Callback> cb) {
  CEF_REQUIRE_UI_THREAD();
  // Centralize the renderer-to-host trust boundary so new bridge methods
  // cannot accidentally omit the UI-browser authorization check.
  if (!IsUiBrowser(browser)) {
    cb->Failure(403, "operation is restricted to the SHELTER UI");
    return true;
  }

#if defined(SHELTER_CI_HOOKS)
  const auto bridge_started = BridgePerfClock::now();
  auto log_bridge_perf = [this, &m, bridge_started]() {
    const auto finished = BridgePerfClock::now();
    const double elapsed_ms = BoundedPerfValue(
        std::chrono::duration<double, std::milli>(finished - bridge_started).count(),
        1000000.0);
    const std::string method = DiagnosticMethod(m);
    BridgePerfBucket& bucket = BridgePerfBuckets()[method];
    ++bucket.count;
    bucket.total_ms = std::min(1000000000.0, bucket.total_ms + elapsed_ms);
    bucket.max_ms = std::max(bucket.max_ms, elapsed_ms);
    const bool first = bucket.last_report == BridgePerfClock::time_point{};
    const bool interval_elapsed = first ||
        finished - bucket.last_report >= std::chrono::seconds(10);
    const bool slow_interval_elapsed = elapsed_ms >= 100.0 &&
        (first || finished - bucket.last_report >= std::chrono::seconds(1));
    if (!interval_elapsed && !slow_interval_elapsed) return;

    std::ostringstream line;
    line << "perf bridge method=" << method << " count=" << bucket.count
         << " total_ms=" << PerfMilliseconds(bucket.total_ms)
         << " max_ms=" << PerfMilliseconds(bucket.max_ms);
    Log(line.str());
    bucket.count = 0;
    bucket.total_ms = 0;
    bucket.max_ms = 0;
    bucket.last_report = finished;
  };
  ScopeExit<decltype(log_bridge_perf)> bridge_perf_scope(std::move(log_bridge_perf));
#endif

  const auto can_reveal_download = [this](const std::string& raw_path) {
    if (raw_path.empty()) return false;
    for (unsigned char c : raw_path) {
      if (c < 0x20 || c == 0x7f || c == '"' || c == '\'') return false;
    }
    const std::filesystem::path candidate = path_utils::PathFromUtf8(raw_path);
    if (!candidate.is_absolute()) return false;
    for (const std::filesystem::path& component : candidate) {
      if (component == "." || component == "..") return false;
    }
    const std::filesystem::path normalized = candidate.lexically_normal();
    const std::filesystem::path downloads =
        path_utils::PathFromUtf8(platform::DownloadsDir()).lexically_normal();
    const bool in_downloads = downloads.is_absolute() &&
                              security::IsPathWithin(downloads, normalized);
    const bool accepted = accepted_download_paths_.count(raw_path) != 0;
    if (!in_downloads && !accepted) return false;
    return security::IsRegularFileWithoutSymlinkOrJunctionChain(normalized);
  };

  if (m == "premium.status") {
    // Fail closed until a server-backed verifier is configured. Client storage
    // and the development checkout are never evidence of a paid entitlement.
    cb->Success("{\"ok\":false,\"verified\":false,\"status\":\"unavailable\"}");
    return true;
  }
  if (m == "premium.checkout") {
    // There is no payment provider in this build; do not create a local order
    // or grant access. A production integration must return only a server order.
    cb->Success("{\"ok\":false,\"error\":\"payment_provider_unavailable\"}");
    return true;
  }

  if (m == "user-data.delete") {
    // Destructive host operation; UI authorization is enforced above.
    if (closing_ || delete_user_data_on_exit()) {
      cb->Failure(409, "application is already closing");
      return true;
    }
    MarkDeleteUserDataOnExit();
    cb->Success("{\"ok\":true,\"closing\":true}");
    // Let cefQuery deliver its acknowledgement and the UI display its final
    // notice before closing the window. Files are removed after CefShutdown.
    if (!CefPostDelayedTask(TID_UI, new CloseAfterUserDataDeleteTask(), 350))
      RequestClose();
    return true;
  }

  // ---- окно ----
  if (m == "win.state") {
    cb->Success(window_ && window_->IsMaximized()
                    ? "{\"maximized\":true}"
                    : "{\"maximized\":false}");
    return true;
  }
  if (m == "win.minimize") {
    if (window_) window_->Minimize();
    cb->Success("{}");
    return true;
  }
  if (m == "win.maximize") {
    if (window_) {
      if (window_->IsMaximized()) window_->Restore(); else window_->Maximize();
    }
    cb->Success("{}");
    return true;
  }
  if (m == "win.close") {
    RequestClose();
    cb->Success("{}");
    return true;
  }
  if (m == "win.fullscreen") {
    SetWindowFullscreen(Flag(a, "on", false));
    cb->Success("{}");
    return true;
  }
  if (m == "favicon.ensure") {
    // Догрузка иконок сайтов по запросу UI (закладки/история без иконки в
    // кэше). Разрешено только небольшой пачкой: это сетевой запрос,
    // инициированный UI, который не должен задаваться сторонним кодом.
    const std::string hosts = Str(a, "hosts");
    if (hosts.size() > 1024) {
      cb->Failure(400, "too many hosts in one request");
      return true;
    }
    RequestFavicons(hosts, browser);
    cb->Success("{}");
    return true;
  }
  if (m == "ext.list") {
    ExtList(cb);
    return true;
  }
  if (m == "ext.setMode") {
    ExtSetMode(Flag(a, "engine", true), cb);
    return true;
  }
  if (m == "ext.install") {
    ExtInstall(Str(a, "src"));
    cb->Success("{}");
    return true;
  }
  if (m == "ext.remove") {
    ExtRemove(Str(a, "id"));
    cb->Success("{}");
    return true;
  }
  if (m == "ext.pick") {
    ExtPick();
    cb->Success("{}");
    return true;
  }
  if (m == "file.open") {  // Ctrl+O: нативный диалог, затем file-open в UI
    PickLocalFile();
    cb->Success("{}");
    return true;
  }
  if (m == "win.openPlain") {  // «Открыть в новом окне» — пока вкладкой
    UiEvent("newtab", "{\"url\":" + JsString(Str(a, "url")) + "}");
    cb->Success("{}");
    return true;
  }

  // ---- вкладки ----
  if (m == "view.open") {
    const std::string id = Str(a, "id");
    const std::string url = Str(a, "url");
    const std::string partition = Str(a, "partition");
    if (id.empty() || !window_) {
      cb->Failure(400, "bad args");
      return true;
    }
    const double zoom = Num(a, "zoom", 1.0);
    Tab* t = FindTab(id);
    if (t && t->partition != partition) {  // сменился профиль (пространство/Призрак)
      DestroyTab(id);
      t = nullptr;
    }
    bool created = false, navigated = false;
    const std::string nav = ConsumeSelectedLocalFileUrl(url) ? url : NavUrl(url);
    if (!t) {
      t = CreateTab(id, partition, nav, zoom);
      created = navigated = true;
    } else if (!SameUrl(url, t->requested_url) && !SameUrl(url, t->current_url)) {
      t->requested_url = url;
      t->error_url.clear();
      if (t->browser) {
        t->browser->GetMainFrame()->LoadURL(nav);
      } else {
        t->pending_url = nav;
      }
      navigated = true;
    }
    active_tab_ = id;
    const bool view_visible = Flag(a, "visible", true);
    LayoutTab(t, RectOf(a), view_visible);
    if (!view_visible) HideInactiveTabs(t);
    if (Flag(a, "focus", true)) {
      if (t->view) {
        t->view->RequestFocus();
      } else {
        t->pending_focus = true;  // профиль ещё инициализируется
      }
    }
    std::ostringstream os;
    os << "{\"created\":" << (created ? "true" : "false") << ",\"navigated\":"
       << (navigated ? "true" : "false") << ",\"loading\":"
       << (t->loading ? "true" : "false") << "}";
    cb->Success(os.str());
    return true;
  }
  if (m == "view.layout") {
    Tab* t = FindTab(active_tab_);
    if (t) LayoutTab(t, RectOf(a), Flag(a, "visible", true));
    cb->Success("{}");
    return true;
  }
  if (m == "view.clip") {  // скругление сцены и цвет фоновой маски
    for (int i = 0; i < 4; ++i) clip_radii_[i] = 0;
    if (a->HasKey("maskColor")) {
      const long long color = std::llround(Num(a, "maskColor", 0));
      clip_mask_color_ = static_cast<std::uint32_t>(
          std::clamp<long long>(color, 0, 0x00ffffff));
    }
    clip_holes_.clear();
    if (a->HasKey("radii") && a->GetType("radii") == VTYPE_LIST) {
      CefRefPtr<CefListValue> l = a->GetList("radii");
      for (size_t i = 0; i < 4 && i < l->GetSize(); ++i) {
        const int ty = l->GetType(i);
        clip_radii_[i] = ty == VTYPE_DOUBLE ? l->GetDouble(i) : ty == VTYPE_INT ? l->GetInt(i) : 0;
      }
    }
    if (a->HasKey("holes") && a->GetType("holes") == VTYPE_LIST) {
      CefRefPtr<CefListValue> l = a->GetList("holes");
      for (size_t i = 0; i < l->GetSize() && i < 16; ++i) {
        if (l->GetType(i) != VTYPE_LIST) continue;
        CefRefPtr<CefListValue> r = l->GetList(i);
        if (r->GetSize() < 4) continue;
        std::array<int, 5> h{};
        for (size_t k = 0; k < r->GetSize() && k < 5; ++k) {
          const int ty = r->GetType(k);
          h[k] = static_cast<int>(std::lround(ty == VTYPE_DOUBLE ? r->GetDouble(k) : ty == VTYPE_INT ? r->GetInt(k) : 0));
        }
        clip_holes_.push_back(h);
      }
    }
    ApplyClip(FindTab(active_tab_));
    cb->Success("{}");
    return true;
  }
  if (m == "view.hide") {
    HideAllTabs();
    if (ui_view_) ui_view_->RequestFocus();  // клавиатура — обратно в UI
    cb->Success("{}");
    return true;
  }
  if (m == "view.show") {  // вернуть активную вкладку после снимка/меню
    Tab* t = FindTab(active_tab_);
    if (t) {
      LayoutTab(t, RectOf(a), true);
      if (Flag(a, "focus", true) && t->view) t->view->RequestFocus();
    }
    cb->Success("{}");
    return true;
  }
  if (m == "tabs.sync") {
    std::set<std::string> keep;
    if (a->HasKey("ids") && a->GetType("ids") == VTYPE_LIST) {
      CefRefPtr<CefListValue> l = a->GetList("ids");
      for (size_t i = 0; i < l->GetSize(); ++i) {
        if (l->GetType(i) == VTYPE_STRING) keep.insert(l->GetString(i).ToString());
      }
    }
    std::vector<std::string> drop;
    for (auto& kv : tabs_) {
      if (!keep.count(kv.first)) drop.push_back(kv.first);
    }
    for (auto& id : drop) DestroyTab(id);
    cb->Success("{}");
    return true;
  }
  if (m == "view.act" || m == "ctx.act") {
    std::string id = Str(a, "id");
    if (id.empty()) id = active_tab_;
    TabAction(id, Str(a, "act"), Str(a, "url"));
    cb->Success("{}");
    return true;
  }
  if (m == "view.snap") {
    CaptureSnapshot(Str(a, "id").empty() ? active_tab_ : Str(a, "id"),
                    static_cast<int>(Num(a, "q", 72)), cb);
    return true;
  }
  if (m == "view.zoom") {
    SetZoomAll(Num(a, "factor", 1.0));
    cb->Success("{}");
    return true;
  }
  if (m == "find") {  // поиск по странице вкладки (нативный)
    Tab* t = FindTab(Str(a, "id"));
    if (t && t->browser) {
      if (Str(a, "act") == "stop") {
        t->browser->GetHost()->StopFinding(true);
      } else {
        const std::string text = Str(a, "q");
        if (text.empty()) {
          t->browser->GetHost()->StopFinding(true);
        } else {
          const bool next = Flag(a, "next", false);
          if (!next) {
            t->find_text = text;
            t->find_kick = true;
          }
          t->browser->GetHost()->Find(text, Flag(a, "forward", true), false,
                                      next);
        }
      }
    }
    cb->Success("{}");
    return true;
  }
  if (m == "devtools") {
    // Настоящий Chromium DevTools в отдельном окне для активной вкладки.
    // Повторный вызов выводит уже открытое окно на передний план (ShowDevTools
    // не плодит копии). Док-режим (встроенная панель) здесь не реализован:
    // act=dock/close отвечают честным ok:false.
    const std::string act = Str(a, "act");
    Tab* devtools_tab = active_tab_.empty() ? nullptr : FindTab(active_tab_);
    if ((act == "window" || act == "open") && devtools_tab &&
        devtools_tab->browser) {
      TabAction(active_tab_, "devtools", "");
      cb->Success("{\"ok\":true}");
    } else {
      cb->Success("{\"ok\":false}");
    }
    return true;
  }

  // ---- система ----
  if (m == "clip.read") {
    cb->Success("{\"text\":" + JsString(platform::ClipboardRead()) + "}");
    return true;
  }
  if (m == "clip.write") {
    const bool ok = platform::ClipboardWrite(Str(a, "text"));
    cb->Success(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return true;
  }
  if (m == "theme.scheme") {
    const bool dark = ColorSchemeDarkFromMode(Str(a, "mode"));
    platform::SetColorScheme(dark);
    // Тема интерфейса сообщается и Chromium: от режима зависят
    // prefers-color-scheme на страницах, скроллбары и элементы управления.
    SetColorSchemeAll(dark);
    cb->Success("{}");
    return true;
  }
  if (m == "shell.show") {
    const std::string path = Str(a, "path");
    if (can_reveal_download(path)) platform::ShowInFolder(path);
    cb->Success("{}");
    return true;
  }
  if (m == "block.setEnabled") {
    // UI-настройка «Блокировка трекеров и рекламы»: включаем/выключаем
    // сетевую и косметическую фильтрацию на нативной стороне.
    blocker::SetEnabled(Flag(a, "on", false));
    cb->Success("{}");
    return true;
  }
  if (m == "fp.setEnabled") {
    // UI-настройка «Анти-отпечаток»: состояние уходит рендерерам, где до JS
    // страниц встраивается шум Canvas/WebGL/Audio.
    Shell::Get().SetFpEnabled(Flag(a, "on", false));
    cb->Success("{}");
    return true;
  }
  if (m == "cookie.setEnabled") {
    // UI-настройка «Автосогласие cookies»: рендерер подтверждает на сайтах
    // минимальный (технический) набор до появления баннера согласия.
    Shell::Get().SetAutoConsentEnabled(Flag(a, "on", false));
    cb->Success("{}");
    return true;
  }
  if (m == "https.setEnabled") {
    // UI-настройка «Безопасный HTTPS + приватный DNS»: автоподъём навигаций
    // на https:// и шифрованный DNS в сетевом стеке профиля.
    Shell::Get().SetHttpsOnlyEnabled(Flag(a, "on", false));
    cb->Success("{}");
    return true;
  }
  if (m == "dns.setProvider") {
    // Провайдер DoH (Cloudflare / Google / Quad9 / системный). Применяется
    // сразу ко всем инициализированным контекстам.
    Shell::Get().SetDohProvider(Str(a, "provider"));
    cb->Success("{}");
    return true;
  }
  if (m == "strict.setEnabled") {
    // UI-настройка «Строгий режим»: сторонние скрипты блокируются на сетевом
    // уровне, пока сайт не получит разрешение пользователя.
    Shell::Get().SetStrictEnabled(Flag(a, "on", false));
    cb->Success("{}");
    return true;
  }
  if (m == "strict.allow") {
    // Пользователь разрешил сторонние скрипты сайту из тоста «Разрешить».
    Shell::Get().StrictAllow(Str(a, "host"));
    cb->Success("{}");
    return true;
  }
  if (m == "perm.allow") {
    // Пользователь выдал сайту запрошенные разрешения из тоста «Разрешить».
    // Действует до перезапуска; страница должна запросить снова (перезагрузка).
    Shell::Get().AllowSitePermissions(
        Str(a, "origin"),
        static_cast<uint32_t>(Num(a, "mask", 0)),
        Flag(a, "media", false));
    cb->Success("{\"ok\":true}");
    return true;
  }
  if (m == "cert.decision") {
    // Решение пользователя по ошибке сертификата: once (продолжить один раз),
    // remember (запомнить отпечаток) или back (отказ). Всё неизвестное натив
    // трактует как отказ.
    Shell::Get().CertDecision(Str(a, "id"), Str(a, "action"));
    cb->Success("{\"ok\":true}");
    return true;
  }
  if (m == "app.checkUpdates") {
    // Проверка обновлений: натив сверяет kAppVersion с tag_name официального
    // releases/latest и отвечает событием "update-status". manual отличает
    // ручную проверку (кнопка «О SHELTER») от автоматической.
    Shell::Get().CheckForUpdates(Flag(a, "manual", false));
    cb->Success("{\"ok\":true}");
    return true;
  }
  if (m == "ghost.setEnabled") {
    // Режим «Призрак»: новые вкладки уходят в in-memory профили на стороне
    // моста; нативная часть при выключении сразу освобождает пустые
    // приватные контексты.
    Shell::Get().SetGhostMode(Flag(a, "on", false));
    cb->Success("{}");
    return true;
  }
  if (m == "shell.filestate") {
    // Проверяем тот же ограниченный набор путей, что и перед «Показать в папке».
    const bool exists = can_reveal_download(Str(a, "path"));
    cb->Success(std::string("{\"exists\":") + (exists ? "true" : "false") + "}");
    return true;
  }
  if (m == "dl.decision" || m == "dl.control") {
    const std::string id = Str(a, "id");
    const std::string action = Str(a, "action");
    const bool ok = m == "dl.decision"
                        ? DownloadDecision(id, action, Flag(a, "showDialog", false))
                        : DownloadControl(id, action);
    cb->Success(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return true;
  }
  if (m == "privacy.clear") {
    ClearPrivacy(a->HasKey("parts") && a->GetType("parts") == VTYPE_LIST
                     ? a->GetList("parts")
                     : nullptr,
                 a->HasKey("opts") && a->GetType("opts") == VTYPE_DICTIONARY
                     ? a->GetDictionary("opts")
                     : nullptr,
                 cb);
    return true;
  }
#if defined(SHELTER_CI_HOOKS)
  if (m == "diag.performance") {
    static BridgePerfClock::time_point last_ui_report{};
    const auto now = BridgePerfClock::now();
    if (last_ui_report != BridgePerfClock::time_point{} &&
        now - last_ui_report < std::chrono::seconds(5)) {
      cb->Success("{}");
      return true;
    }
    if (!a || !a->HasKey("samples") || a->GetType("samples") != VTYPE_LIST) {
      cb->Success("{}");
      return true;
    }
    CefRefPtr<CefListValue> samples = a->GetList("samples");
    std::ostringstream line;
    line << "perf ui";
    bool has_sample = false;
    for (size_t i = 0; samples && i < samples->GetSize() && i < 64; ++i) {
      if (samples->GetType(i) != VTYPE_DICTIONARY) continue;
      CefRefPtr<CefDictionaryValue> sample = samples->GetDictionary(i);
      const std::string method = Str(sample, "method");
      if (!DiagnosticMethodNames().count(method)) continue;
      const double count_value = BoundedPerfValue(Num(sample, "count"), 1000000.0);
      const std::uint64_t count = static_cast<std::uint64_t>(count_value);
      if (!count) continue;
      const double total_ms = BoundedPerfValue(Num(sample, "totalMs"), 1000000000.0);
      const double max_ms = BoundedPerfValue(Num(sample, "maxMs"), 1000000.0);
      line << " method=" << method << " count=" << count
           << " total_ms=" << PerfMilliseconds(total_ms)
           << " max_ms=" << PerfMilliseconds(max_ms);
      has_sample = true;
    }
    if (has_sample) {
      last_ui_report = now;
      Log(line.str());
    }
    cb->Success("{}");
    return true;
  }
  if (m == "dbg.protections") {  // диагностика (CI): состояние защит в браузерном процессе
    // Приёмка обязана уметь отличить «тумблер не дошёл до нативной части» от
    // «дошёл, но рендерер применил старое значение»: без этого сбой в цепочке
    // выглядит одинаково и объясняется догадками.
    const int flags = Shell::Get().ProtectionFlags();
    cb->Success(std::string("{\"fp\":") +
                ((flags & 1) ? "true" : "false") + ",\"cookies\":" +
                ((flags & 2) ? "true" : "false") + "}");
    return true;
  }
  if (m == "dbg.window") {  // диагностика (CI): состояние главного окна
    CefRefPtr<CefWindow> window = Shell::Get().window();
    const bool maximized = window.get() && window->IsMaximized();
    const bool fullscreen = window.get() && window->IsFullscreen();
    std::ostringstream out;
    out << "{\"maximized\":" << (maximized ? "true" : "false")
        << ",\"fullscreen\":" << (fullscreen ? "true" : "false") << "}";
    cb->Success(out.str());
    return true;
  }
  if (m == "dbg.hit") {  // диагностика hitTest (macOS), только для CI
    cb->Success(JsString(platform::DebugHitTest(Num(a, "x", 0), Num(a, "y", 0))));
    return true;
  }
  if (m == "dbg.clip") {  // диагностика (CI): на каком предке HWND держать область
    Tab* t = FindTab(active_tab_);
    if (t && t->browser) {
      // сбросить область на прежнем окне
      double zr[4] = {0, 0, 0, 0};
      std::vector<std::array<int, 5>> none;
      platform::ApplyViewClip(
          reinterpret_cast<void*>(t->browser->GetHost()->GetWindowHandle()), zr,
          none, last_rect_.x, last_rect_.y,
          std::max(1, last_rect_.width), std::max(1, last_rect_.height), false,
          clip_mask_color_);
      if (a->HasKey("level")) platform::SetClipLevel(static_cast<int>(Num(a, "level", -1)));
      t->has_last_clip = false;
      ApplyClip(t);
      cb->Success(JsString(platform::DumpWindowChain(
          reinterpret_cast<void*>(t->browser->GetHost()->GetWindowHandle()))));
    } else {
      cb->Success("\"no tab\"");
    }
    return true;
  }
#endif  // SHELTER_CI_HOOKS

  // ---- внешний ИИ-агент ----
  if (m == "ai.test" || m == "ai.chat") {
    // Асинхронный HTTPS к провайдеру; вердикт придёт в cb позже.
    return StartAiRequest(browser, m == "ai.chat", a, cb);
  }
  return false;
}

// ---- действия над вкладкой -------------------------------------------------

void Shell::TabAction(const std::string& id, const std::string& act,
                      const std::string& url) {
  Tab* t = FindTab(id);
  if (!t || !t->browser) return;
  CefRefPtr<CefBrowser> b = t->browser;
  CefRefPtr<CefFrame> f = b->GetFocusedFrame();
  if (!f) f = b->GetMainFrame();

  if (act == "reload" || act == "reloadHard") {
    if (!t->error_url.empty()) {
      b->GetMainFrame()->LoadURL(t->error_url);
    } else if (act == "reloadHard") {
      b->ReloadIgnoreCache();
    } else {
      b->Reload();
    }
  } else if (act == "stop") {
    b->StopLoad();
  } else if (act == "print") {
    b->GetHost()->Print();
  } else if (act == "devtools") {
    CefWindowInfo wi;
    CefBrowserSettings bs;
    b->GetHost()->ShowDevTools(wi, nullptr, bs, CefPoint());
  } else if (act == "inspect") {
    // «Инспектировать»: DevTools открывается сразу на элементе под курсором.
    // Координаты — «x,y» в DIP относительно вида вкладки (из события ctx).
    int x = 0, y = 0;
    const size_t comma = url.find(',');
    if (comma != std::string::npos) {
      x = std::atoi(url.substr(0, comma).c_str());
      y = std::atoi(url.substr(comma + 1).c_str());
    }
    CefWindowInfo wi;
    CefBrowserSettings bs;
    b->GetHost()->ShowDevTools(wi, nullptr, bs,
                               CefPoint(std::max(0, x), std::max(0, y)));
  } else if (act == "cut") {
    f->Cut();
  } else if (act == "copy") {
    f->Copy();
  } else if (act == "paste") {
    f->Paste();
  } else if (act == "selectAll") {
    f->SelectAll();
  } else if (act == "download" && !url.empty()) {
    b->GetHost()->StartDownload(url);
  }
}

void Shell::SetZoomAll(double factor) {
  factor = std::min(5.0, std::max(0.25, factor));
  zoom_ = factor;
  const double level = std::log(factor) / std::log(1.2);
  for (auto& kv : tabs_) {
    if (kv.second.browser) kv.second.browser->GetHost()->SetZoomLevel(level);
  }
}

void Shell::CaptureSnapshot(const std::string& tab_id, int quality,
                            CefRefPtr<Callback> cb) {
  Tab* t = FindTab(tab_id);
  if (!t || !t->browser) {
    cb->Failure(404, "no tab");
    return;
  }
  if (!t->snap_observer) {
    t->snap_observer = new SnapObserver();
    t->snap_registration =
        t->browser->GetHost()->AddDevToolsMessageObserver(t->snap_observer);
  }
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "jpeg");
  params->SetInt("quality", std::min(100, std::max(10, quality)));
  params->SetBool("optimizeForSpeed", true);
  const int id = t->browser->GetHost()->ExecuteDevToolsMethod(
      0, "Page.captureScreenshot", params);
  if (id == 0) {
    cb->Failure(500, "devtools unavailable");
    return;
  }
  static_cast<SnapObserver*>(t->snap_observer.get())->Add(id, cb);
}

// ---- «Удалить данные» ------------------------------------------------------

void Shell::ClearPrivacy(CefRefPtr<CefListValue> parts,
                         CefRefPtr<CefDictionaryValue> opts,
                         CefRefPtr<Callback> cb) {
  std::set<std::string> mask;
  bool full = true;  // без mask (кнопка «Сжечь») — полная очистка раздела
  double begin = 0;
  if (opts && opts->HasKey("mask") && opts->GetType("mask") == VTYPE_LIST) {
    full = false;
    CefRefPtr<CefListValue> l = opts->GetList("mask");
    for (size_t i = 0; i < l->GetSize(); ++i) {
      if (l->GetType(i) == VTYPE_STRING) mask.insert(l->GetString(i).ToString());
    }
    begin = Num(opts, "begin", 0);
  }

  std::vector<std::string> names;
  if (parts) {
    for (size_t i = 0; i < parts->GetSize(); ++i) {
      if (parts->GetType(i) == VTYPE_STRING) names.push_back(parts->GetString(i).ToString());
    }
  } else {
    for (auto& kv : contexts_) names.push_back(kv.first);
  }

  auto wants = [&](const char* k) { return full || mask.count(k) > 0; };

  for (const auto& p : names) {
    auto it = contexts_.find(p);
    if (it != contexts_.end()) {
      if (wants("COOKIES")) {
        CefRefPtr<CefCookieManager> cm = it->second->GetCookieManager(nullptr);
        if (cm) cm->VisitAllCookies(new CookieClearVisitor(full ? 0 : begin));
      }
      // Живой браузер раздела для DevTools-команд (кэш и хранилища — общие
      // на профиль, достаточно одного; вкладки может и не быть — например,
      // раздел вычищают после закрытия всех его вкладок).
      CefRefPtr<CefBrowser> any;
      for (auto& kv : tabs_) {
        if (kv.second.partition == p && kv.second.browser) {
          any = kv.second.browser;
          break;
        }
      }
      if (wants("CACHE") && any) {
        any->GetHost()->ExecuteDevToolsMethod(0, "Network.clearBrowserCache", nullptr);
      }
      const bool clear_storage = wants("LOCAL_STORAGE") || wants("INDEXED_DB") ||
                                 wants("SERVICE_WORKERS");
      if (clear_storage && any) {
        std::string types;
        auto add = [&](const char* s) { types += (types.empty() ? "" : ",") + std::string(s); };
        if (wants("LOCAL_STORAGE")) add("local_storage");
        if (wants("INDEXED_DB")) add("indexeddb");
        if (wants("SERVICE_WORKERS")) { add("service_workers"); add("cache_storage"); }
        // Origins: весь журнал раздела за сессию, а не только открытые
        // вкладки, — иначе хранилища закрытых сайтов переживали бы частичную
        // очистку. Адреса открытых вкладок добавляем тоже — журнал мог
        // переполниться и вытеснить старые записи.
        std::set<std::string> origins;
        auto known = partition_origins_.find(p);
        if (known != partition_origins_.end()) origins = known->second;
        for (auto& kv : tabs_) {
          Tab& t = kv.second;
          if (t.partition != p || !t.browser) continue;
          CefURLParts up;
          if (CefParseURL(t.current_url, up)) {
            const std::string origin = CefString(&up.origin).ToString();
            if (!origin.empty() && origin != "null") origins.insert(origin);
          }
        }
        for (const auto& origin : origins) {
          CefRefPtr<CefDictionaryValue> dp = CefDictionaryValue::Create();
          dp->SetString("origin", origin);
          dp->SetString("storageTypes", types);
          any->GetHost()->ExecuteDevToolsMethod(0, "Storage.clearDataForOrigin", dp);
        }
        partition_origins_.erase(p);  // журнал отчищен — начнётся заново
      }
    }
    if (full) {
      QueueWipe(p);  // каталог профиля удалится при следующем запуске
      partition_origins_.erase(p);
    }
  }
  cb->Success("{\"ok\":true}");
}

}  // namespace shelter
