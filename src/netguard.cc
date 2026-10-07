#include "src/netguard.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "src/common.h"
#include "src/shell.h"

namespace shelter {
namespace netguard {

namespace {

std::atomic<bool> g_https_only{false};

std::mutex g_mu;
std::string g_doh_provider = "cf";
// https://-адрес → исходный http://-адрес (для интерстишиала).
std::map<std::string, std::string> g_upgraded;
// Хосты, которым пользователь разрешил незашифрованный HTTP (до перезапуска).
std::set<std::string> g_insecure_allowed;

constexpr size_t kMaxUpgradeTable = 256;
constexpr size_t kMaxAllowList = 1024;

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

// Разбирает IPv4-адрес; возвращает false, если это не точечная четверка.
bool ParseIpv4(const std::string& host, int octets[4]) {
  int parts[4];
  char tail;
  if (std::sscanf(host.c_str(), "%d.%d.%d.%d%c", &parts[0], &parts[1],
                  &parts[2], &parts[3], &tail) != 4) {
    return false;
  }
  for (int i = 0; i < 4; i++) {
    if (parts[i] < 0 || parts[i] > 255) return false;
    octets[i] = parts[i];
  }
  return true;
}

// Loopback, линк-локал, частные диапазоны, CGNAT — как Chrome в режиме
// «публичные сайты»: такие адреса не апгрейдятся (роутеры, принтеры,
// интранет-порталы почти никогда не имеют валидного сертификата).
bool IsPrivateHost(std::string host) {
  host = Lower(host);
  if (host.empty()) return true;
  if (!host.empty() && host.front() == '[' && host.back() == ']')
    host = host.substr(1, host.size() - 2);  // IPv6 в скобках
  if (host == "localhost" || host == "::1") return true;
  if (host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0)
    return true;
  if (host.compare(0, 4, "fe80", 4) == 0 || host.compare(0, 2, "fc") == 0 ||
      host.compare(0, 2, "fd") == 0) {
    return true;
  }
  int o[4];
  if (ParseIpv4(host, o)) {
    if (o[0] == 127 || o[0] == 0 || o[0] == 10) return true;
    if (o[0] == 169 && o[1] == 254) return true;       // link-local
    if (o[0] == 192 && o[1] == 168) return true;
    if (o[0] == 172 && o[1] >= 16 && o[1] <= 31) return true;
    if (o[0] == 100 && o[1] >= 64 && o[1] <= 127) return true;  // CGNAT
    return false;
  }
  return false;
}

std::string HostOf(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(CefString(url), parts)) return std::string();
  return Lower(CefString(&parts.host).ToString());
}

}  // namespace

void SetHttpsOnlyEnabled(bool on) { g_https_only.store(on); }

bool HttpsOnlyEnabled() { return g_https_only.load(); }

void SetDohProvider(const std::string& provider) {
  const std::string p = Lower(provider);
  std::lock_guard<std::mutex> lk(g_mu);
  g_doh_provider = (p == "cf" || p == "google" || p == "quad9" || p == "sys")
                       ? p
                       : "cf";
}

std::string DohProvider() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_doh_provider;
}

std::string DohTemplates(const std::string& provider) {
  // Публичные DoH-эндпоинты (актуальные на октябрь 2026; формат — шаблон
  // URI RFC 6570, как ожидает Chromium в «dns_over_https.templates»).
  if (provider == "cf") return "https://cloudflare-dns.com/dns-query";
  if (provider == "google") return "https://dns.google/dns-query";
  if (provider == "quad9") return "https://dns.quad9.net/dns-query";
  return std::string();
}

void ApplyDohPrefs(CefRefPtr<CefRequestContext> context) {
  if (!context.get() || !CefCurrentlyOn(TID_UI)) return;
  const std::string provider = DohProvider();
  const std::string tmpl = DohTemplates(provider);
  const bool secure = HttpsOnlyEnabled() && !tmpl.empty();
  auto set = [&context](const char* name, const std::string& value) {
    CefRefPtr<CefValue> v = CefValue::Create();
    v->SetString(value);
    CefString error;
    if (!context->SetPreference(name, v, error)) {
      LOG(WARNING) << "netguard: SetPreference(" << name
                   << ") failed: " << error.ToString();
    }
  };
  if (secure) {
    // Сначала шаблон, затем режим — чтобы сеть не увидела «secure» без
    // валидного эндпоинта.
    set("dns_over_https.templates", tmpl);
    set("dns_over_https.mode", "secure");
  } else {
    set("dns_over_https.mode", "off");
    set("dns_over_https.templates", std::string());
  }
}

std::optional<std::string> TryUpgrade(const std::string& url) {
  if (!HttpsOnlyEnabled()) return std::nullopt;
  if (url.rfind("http://", 0) != 0) return std::nullopt;
  CefURLParts parts;
  if (!CefParseURL(CefString(url), parts)) return std::nullopt;
  // Явный порт почти наверняка не слушает TLS — апгрейд сломает адрес.
  if (parts.port.length > 0) return std::nullopt;
  const std::string host = Lower(CefString(&parts.host).ToString());
  if (host.empty() || host.find('.') == std::string::npos) return std::nullopt;
  if (IsPrivateHost(host)) return std::nullopt;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_insecure_allowed.count(host)) return std::nullopt;
  }
  return "https://" + url.substr(7);
}

void RememberUpgrade(const std::string& https_url, const std::string& http_url) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_upgraded.size() >= kMaxUpgradeTable) g_upgraded.erase(g_upgraded.begin());
  g_upgraded[https_url] = http_url;
}

std::optional<std::string> TakeUpgradeFallback(const std::string& https_url) {
  std::string http_url;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_upgraded.find(https_url);
    if (it == g_upgraded.end()) return std::nullopt;
    http_url = it->second;
    g_upgraded.erase(it);
  }
  // Раз показали интерстишиал — запоминаем исключение: дальше сайт
  // открывается как есть, без повторных апгрейдов (до перезапуска).
  const std::string host = HostOf(http_url);
  if (!host.empty()) AllowInsecure(host);
  return http_url;
}

void AllowInsecure(const std::string& host) {
  const std::string h = Lower(host);
  if (h.empty()) return;
  std::lock_guard<std::mutex> lk(g_mu);
  g_insecure_allowed.insert(h);
  if (g_insecure_allowed.size() > kMaxAllowList)
    g_insecure_allowed.erase(g_insecure_allowed.begin());
}

void RecordUpgrade(const std::string& host) {
  // Вызывается с IO-потока; статистику ведём в UI-потоке.
  CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                          [](std::string h) {
                            std::ostringstream os;
                            os << "{\"host\":" << JsString(h) << ",\"h\":1}";
                            Shell::Get().UiEvent("blocked", os.str());
                          },
                          host)));
}

}  // namespace netguard
}  // namespace shelter
