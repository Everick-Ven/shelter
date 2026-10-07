#include "src/blocker.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "src/common.h"
#include "src/platform.h"
#include "src/shell.h"

namespace shelter {
namespace blocker {

namespace {

std::mutex g_mu;
std::set<std::string> g_domains;     // регистрируемые домены (lowercase)
std::vector<std::string> g_patterns; // подстроки URL (lowercase)
std::string g_cosmetic;              // cosmetic.css
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_loaded{false};

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

std::string HostOf(const std::string& url) {
  CefURLParts parts;
  if (!CefParseURL(CefString(url), parts)) return std::string();
  return Lower(CefString(&parts.host).ToString());
}

// Регистрируемый домен для сравнения first-party / third-party, как в
// настоящих блокировщиках. Без публичной PSL-таблицы CEF берём последние два
// ярлыка (три для известных двухуровневых суффиксов): эвристика симметрична
// для обеих сторон сравнения, поэтому ложные срабатывания маловероятны.
std::string Registrable(const std::string& host_in) {
  const std::string host = Lower(host_in);
  static const char* const kTwoLevel[] = {
      "co.uk", "org.uk", "net.uk", "gov.uk", "ac.uk", "com.au", "net.au",
      "org.au", "co.nz", "co.jp", "ne.jp", "or.jp", "com.br", "com.mx",
      "com.ar", "com.tr", "com.pl", "com.ua", "net.ru", "org.ru", "com.ru",
      "pp.ru", "msk.ru", "spb.ru", "co.il", "co.kr", "co.in", "com.cn",
      "net.cn", "org.cn", "com.hk", "com.sg", "com.tw", "co.za", "co.id"};
  std::vector<std::string> labels;
  size_t start = 0;
  for (size_t i = 0; i <= host.size(); ++i) {
    if (i == host.size() || host[i] == '.') {
      if (i > start) labels.push_back(host.substr(start, i - start));
      start = i + 1;
    }
  }
  if (labels.size() <= 2) return host;
  const std::string last2 = labels[labels.size() - 2] + "." + labels.back();
  const size_t take =
      std::find(kTwoLevel, kTwoLevel + (sizeof(kTwoLevel) / sizeof(kTwoLevel[0])),
                last2) != kTwoLevel + (sizeof(kTwoLevel) / sizeof(kTwoLevel[0]))
          ? 3
          : 2;
  std::string out;
  for (size_t i = labels.size() - take; i < labels.size(); ++i) {
    if (!out.empty()) out += ".";
    out += labels[i];
  }
  return out;
}

void LoadList(const std::string& path, std::set<std::string>* domains,
              std::vector<std::string>* patterns) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    const size_t hash = line.find('#');  // комментарии
    if (hash != std::string::npos) line.resize(hash);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t'))
      line.pop_back();
    const size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) continue;
    const std::string v = Lower(line.substr(start));
    if (v.empty()) continue;
    if (domains) domains->insert(v);
    if (patterns) patterns->push_back(v);
  }
}

}  // namespace

void LoadRules() {
  std::set<std::string> domains;
  std::vector<std::string> patterns;
  std::string cosmetic;
  const std::string dir = platform::FiltersDir();
  LoadList(dir + "/tracker-domains.txt", &domains, nullptr);
  LoadList(dir + "/url-patterns.txt", nullptr, &patterns);
  {
    std::ifstream f(dir + "/cosmetic.css", std::ios::binary);
    if (f) {
      cosmetic.assign((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    }
  }
  {
    std::lock_guard<std::mutex> lk(g_mu);
    g_domains.swap(domains);
    g_patterns.swap(patterns);
    g_cosmetic.swap(cosmetic);
  }
  g_loaded = true;
}

void SetEnabled(bool on) {
  if (on && !g_loaded.load()) LoadRules();
  g_enabled = on;
}

bool Enabled() { return g_enabled.load(); }

bool ShouldBlock(const std::string& url, const std::string& page_url,
                 char* kind) {
  if (!g_enabled.load(std::memory_order_relaxed)) return false;
  const std::string lower = Lower(url);

  std::lock_guard<std::mutex> lk(g_mu);
  // 1) Шаблоны URL — рекламные пути/скрипты, в том числе first-party.
  for (const std::string& p : g_patterns) {
    if (!p.empty() && lower.find(p) != std::string::npos) {
      if (kind) *kind = 'a';
      return true;
    }
  }

  // 2) Домены-трекеры — только сторонние запросы, чтобы не ломать сайты,
  // которые действительно размещены на этих доменах (first-party).
  const std::string host = HostOf(url);
  if (host.empty()) return false;
  const std::string req_reg = Registrable(host);
  const std::string page_reg = Registrable(HostOf(page_url));
  if (!req_reg.empty() && req_reg == page_reg) return false;

  for (const std::string& d : g_domains) {
    if (host == d ||
        (host.size() > d.size() &&
         host.compare(host.size() - d.size() - 1, d.size() + 1,
                      "." + d) == 0)) {
      if (kind) *kind = 't';
      return true;
    }
  }
  return false;
}

void RecordBlock(const std::string& host, char kind) {
  // Вызывается с IO-потока CEF; статистику и UI-событие ведём в UI-потоке.
  const bool tracker = kind == 't';
  CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(
                          [](std::string h, bool t) {
                            std::ostringstream os;
                            os << "{\"host\":" << JsString(h)
                               << ",\"t\":" << (t ? 1 : 0)
                               << ",\"a\":" << (t ? 0 : 1) << "}";
                            Shell::Get().UiEvent("blocked", os.str());
                          },
                          host, tracker)));
}

const std::string& CosmeticCss() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_cosmetic;
}

}  // namespace blocker
}  // namespace shelter
