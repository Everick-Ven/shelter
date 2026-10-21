#include "src/netguard.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
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
#include "src/net_utils.h"
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

// Классификация приватных хостов и нижний регистр живут в src/net_utils.h —
// там же контракт (tests/net_utils_test.cc): IPv6-ветка только для литералов
// с ':', fe80::/10 целиком, «localhost.» и IPv4-mapped покрыты.

std::string HostOf(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(CefString(url), parts)) return std::string();
  return LowerHost(CefString(&parts.host).ToString());
}

}  // namespace

void SetHttpsOnlyEnabled(bool on) { g_https_only.store(on); }

bool HttpsOnlyEnabled() { return g_https_only.load(); }

void SetDohProvider(const std::string& provider) {
  const std::string p = LowerHost(provider);
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

void ApplyPrivacyPrefs(CefRefPtr<CefRequestContext> context) {
  if (!context.get() || !CefCurrentlyOn(TID_UI)) return;
  auto set = [&context](const char* name, CefRefPtr<CefValue> value) {
    CefString error;
    if (!context->SetPreference(name, value, error)) {
      LOG(WARNING) << "netguard: SetPreference(" << name
                   << ") failed: " << error.ToString();
    }
  };
  // Сторонние cookie — главный механизм сквозного трекинга: запрещаем явно,
  // а не дефолтом движка. Оба префа про одно и то же (устаревший bool и
  // актуальный режим) — ставим согласованно.
  CefRefPtr<CefValue> block_3p = CefValue::Create();
  block_3p->SetBool(true);
  set("profile.block_third_party_cookies", block_3p);
  CefRefPtr<CefValue> cookie_mode = CefValue::Create();
  cookie_mode->SetInt(1);  // CookieControlsMode::kBlockThirdParty
  set("profile.cookie_controls_mode", cookie_mode);
  // Рекламные API Privacy Sandbox (Topics, Attribution Reporting, FLEDGE):
  // измерения для рекламодателей приватному браузеру не нужны — целиком off.
  CefRefPtr<CefValue> sandbox_off = CefValue::Create();
  sandbox_off->SetBool(false);
  set("privacy_sandbox.apis_enabled", sandbox_off);
}

std::optional<std::string> TryUpgrade(const std::string& url) {
  if (!HttpsOnlyEnabled()) return std::nullopt;
  if (url.rfind("http://", 0) != 0) return std::nullopt;
  CefURLParts parts;
  if (!CefParseURL(CefString(url), parts)) return std::nullopt;
  // Явный порт почти наверняка не слушает TLS — апгрейд сломает адрес.
  if (parts.port.length > 0) return std::nullopt;
  const std::string host = LowerHost(CefString(&parts.host).ToString());
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
  const std::string h = LowerHost(host);
  if (h.empty()) return;
  std::lock_guard<std::mutex> lk(g_mu);
  g_insecure_allowed.insert(h);
  if (g_insecure_allowed.size() > kMaxAllowList)
    g_insecure_allowed.erase(g_insecure_allowed.begin());
}

void RecordUpgrade(const std::string& host, int browser_id) {
  // Вызывается с IO-потока; статистику ведём в UI-потоке, там же резолвим
  // вкладку-источник по id браузера.
  CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                          [](std::string h, int bid) {
                            std::ostringstream os;
                            os << "{\"host\":" << JsString(h) << ",\"h\":1"
                               << ",\"id\":" << JsString(Shell::Get().TabIdForBrowser(bid))
                               << "}";
                            Shell::Get().UiEvent("blocked", os.str());
                          },
                          host, browser_id)));
}

// ---- строгий режим ----------------------------------------------------------

namespace {

std::atomic<bool> g_strict{false};

// Разрешённые пользователем сайты (регистрируемые домены). Сессия в памяти.
std::mutex g_strict_mu;
std::set<std::string> g_script_allowed;
// Троттлинг интерактивных уведомлений: домен страницы -> время последнего.
std::map<std::string, int64_t> g_strict_last_notify;
constexpr int64_t kStrictNotifyCooldownMs = 6000;
constexpr size_t kMaxScriptAllowList = 512;

}  // namespace

void SetStrictEnabled(bool on) { g_strict.store(on); }

bool StrictEnabled() { return g_strict.load(); }

void AllowScriptsFor(const std::string& host) {
  const std::string reg = RegistrableDomain(host);
  if (reg.empty() || reg.find('.') == std::string::npos) return;
  std::lock_guard<std::mutex> lk(g_strict_mu);
  g_script_allowed.insert(reg);
  if (g_script_allowed.size() > kMaxScriptAllowList)
    g_script_allowed.erase(g_script_allowed.begin());
}

bool ScriptsAllowedFor(const std::string& host) {
  const std::string reg = RegistrableDomain(host);
  std::lock_guard<std::mutex> lk(g_strict_mu);
  return g_script_allowed.count(reg) > 0;
}

bool ShouldBlockScript(const std::string& script_url,
                       const std::string& page_url) {
  if (!StrictEnabled()) return false;
  if (script_url.rfind("http://", 0) != 0 &&
      script_url.rfind("https://", 0) != 0)
    return false;
  const std::string script_host = HostOf(script_url);
  const std::string page_host = HostOf(page_url);
  if (script_host.empty()) return false;
  // Инлайн/внутренние и служебные источники не трогаем.
  if (page_host.empty()) return false;
  const std::string s_reg = RegistrableDomain(script_host);
  const std::string p_reg = RegistrableDomain(page_host);
  // Свой домен (включая поддомены) — не сторонний.
  if (!s_reg.empty() && s_reg == p_reg) return false;
  // Сайту уже разрешили сторонние скрипты.
  if (ScriptsAllowedFor(page_host)) return false;
  return true;
}

void RecordScriptBlock(const std::string& page_host, int browser_id,
                       const std::string& script_host) {
  const std::string p_reg = RegistrableDomain(page_host);
  // Интерактивное уведомление показываем не чаще раза в несколько секунд на
  // сайт, чтобы не заваливать тостами при десятках заблокированных скриптов.
  bool notify = false;
  {
    std::lock_guard<std::mutex> lk(g_strict_mu);
    const int64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    auto it = g_strict_last_notify.find(p_reg);
    if (it == g_strict_last_notify.end() ||
        now_ms - it->second >= kStrictNotifyCooldownMs) {
      g_strict_last_notify[p_reg] = now_ms;
      notify = true;
      if (g_strict_last_notify.size() > kMaxScriptAllowList)
        g_strict_last_notify.erase(g_strict_last_notify.begin());
    }
  }
  CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                          [](std::string page, std::string script, bool n, int bid) {
                            std::ostringstream sb;
                            sb << "{\"s\":1,\"id\":"
                               << JsString(Shell::Get().TabIdForBrowser(bid)) << "}";
                            Shell::Get().UiEvent("blocked", sb.str());
                            if (n) {
                              std::ostringstream os;
                              os << "{\"host\":" << JsString(page)
                                 << ",\"script\":" << JsString(script) << "}";
                              Shell::Get().UiEvent("strict-block", os.str());
                            }
                          },
                          page_host, script_host, notify, browser_id)));
}

}  // namespace netguard
}  // namespace shelter
