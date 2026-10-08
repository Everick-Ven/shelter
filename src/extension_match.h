// Правила совпадения для content_scripts расширений (match-patterns и globs).
//
// CEF вырезал публичный API расширений (~M127), поэтому оболочка сама
// выполняет ту часть модели Chromium-расширений, которая не требует Chrome-UI:
// объявленные в манифесте content_scripts (JS/CSS) внедряются в подходящие
// фреймы вкладок. Чтобы поведение совпадало с Chromium, разбор
// `matches` / `exclude_matches` / `include_globs` / `exclude_globs` /
// `all_frames` живёт здесь и покрыт обычным c++ тестом
// (tests/extension_match_test.cc) — CEF для проверки не нужен.
//
// Семантика (документация Chrome «Content scripts»):
//   * matches         — должен совпасть хотя бы один шаблон;
//   * exclude_matches — при совпадении любого скрипт не внедряется;
//   * include_globs   — если список не пуст, URL обязан совпасть хотя бы с
//                       одним из глобов (это дополнительный фильтр);
//   * exclude_globs   — при совпадении любого скрипт не внедряется;
//   * all_frames      — внедрять и в подфреймы (иначе только главный фрейм).
//
// Шаблон match-pattern:
//   <scheme>://<host><path>, где <scheme> — http/https/*, <host> — домен,
//   *.домен или *, <path> — часть пути со `*`. Схема `*` означает только
//   http/https (как в Chromium), портов в шаблонах нет. Query и fragment в
//   сравнении пути не участвуют (сравнивается путь), globs же сравниваются со
//   всей строкой URL.
//
// `<all_urls>` соответствует http/https. Локальные file:// страницы намеренно
// не затрагиваются: расширения (как и в Chrome без отдельного разрешения) не
// получают доступ к файлам на диске.
#ifndef SHELTER_EXTENSION_MATCH_H_
#define SHELTER_EXTENSION_MATCH_H_

#include <cctype>
#include <cstring>
#include <regex>
#include <string>
#include <vector>

namespace shelter {

// Разбор URL на схему/хост/путь (без query и fragment). Возвращает false для
// URL без схемы (about:blank, data:, javascript:) — такие фреймы расширения не
// получают. Схема и хост приводятся к нижнему регистру, путь — нет.
inline bool ExtSplitUrl(const std::string& url, std::string* scheme,
                        std::string* host, std::string* path) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0) return false;
  const auto lower = [](std::string s) {
    for (char& c : s)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  const std::string rest = url.substr(scheme_end + 3);
  const auto host_end = rest.find_first_of("/?#");
  std::string h =
      host_end == std::string::npos ? rest : rest.substr(0, host_end);
  // Порт в сравнении хоста не участвует (в шаблонах Chrome его и нет).
  const auto colon = h.find(':');
  if (colon != std::string::npos && colon + 1 < h.size() &&
      std::isdigit(static_cast<unsigned char>(h[colon + 1]))) {
    h = h.substr(0, colon);
  }
  std::string p =
      host_end == std::string::npos ? std::string() : rest.substr(host_end);
  const auto path_end = p.find_first_of("?#");
  if (path_end != std::string::npos) p = p.substr(0, path_end);
  if (p.empty() || p[0] != '/') p = "/";
  if (h.empty()) return false;
  *scheme = lower(url.substr(0, scheme_end));
  *host = lower(h);
  *path = std::move(p);
  return true;
}

// URL в виде, пригодном для сравнения с шаблоном: без query и fragment (в
// шаблонах Chrome они не участвуют), схема и хост в нижнем регистре (хосты
// сравниваются без учёта регистра) и с путём — если пути нет, подставляется
// «/». Так `*://example.com/*` совпадает и с `https://EXAMPLE.com`.
inline std::string ExtNormalizeUrl(const std::string& url) {
  const auto cut = url.find_first_of("?#");
  const std::string u = cut == std::string::npos ? url : url.substr(0, cut);
  const auto sep = u.find("://");
  if (sep == std::string::npos) return u;
  const auto host_begin = sep + 3;
  const auto host_end = u.find('/', host_begin);
  const std::string host =
      host_end == std::string::npos
          ? u.substr(host_begin)
          : u.substr(host_begin, host_end - host_begin);
  std::string out = u.substr(0, host_begin);  // схема + "://"
  for (char& c : out)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (const char c : host)
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (host_end == std::string::npos) out += '/';
  else out += u.substr(host_end);
  return out;
}

// Шаблон -> regex: `*` превращается в «любые символы», остальное экранируется.
inline std::string ExtPatternToRegex(const std::string& s) {
  std::string r;
  r.reserve(s.size() + 8);
  for (const char c : s) {
    if (c == '*') {
      r += ".*";
    } else if (std::strchr(".+?[]^$(){}|\\", c) != nullptr) {
      r += '\\';
      r += c;
    } else {
      r += c;
    }
  }
  return r;
}

// Глоб -> regex: `*` — любые символы, `?` — один символ, остальное экранируется.
inline std::string ExtGlobToRegex(const std::string& glob) {
  std::string r;
  r.reserve(glob.size() + 8);
  for (const char c : glob) {
    if (c == '*') {
      r += ".*";
    } else if (c == '?') {
      r += ".";
    } else if (std::strchr(".+?[]^$(){}|\\", c) != nullptr) {
      r += '\\';
      r += c;
    } else {
      r += c;
    }
  }
  return r;
}

// Match-pattern -> regex (без якорей). `(?!)` — шаблон, который не совпадает
// ни с чем (невалидный: нет схемы, пустой хост, порт в хосте).
inline std::string ExtUrlPatternToRegex(const std::string& pattern) {
  if (pattern == "<all_urls>") return "(?:http|https)://[^/?#]+(?:[/?#].*)?";
  const auto sep = pattern.find("://");
  if (sep == std::string::npos || sep == 0) return "(?!)";
  std::string pscheme = pattern.substr(0, sep);
  for (char& c : pscheme)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const std::string rest = pattern.substr(sep + 3);
  const auto slash = rest.find('/');
  std::string phost = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string ppath = slash == std::string::npos ? "/" : rest.substr(slash);
  if (ppath.empty()) ppath = "/";
  if (phost.empty() || phost.find(':') != std::string::npos) return "(?!)";
  for (char& c : phost)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  // `*` в схеме — только http/https (как в Chromium), не любой протокол.
  const std::string scheme_re =
      pscheme == "*" ? "(?:http|https)" : ExtPatternToRegex(pscheme);
  std::string hpat;
  if (phost == "*") {
    hpat = "[^/?#]+";
  } else if (phost[0] == '*') {  // *.example.com — домен и его поддомены
    std::string suffix = phost.substr(1);
    if (!suffix.empty() && suffix[0] == '.') suffix = suffix.substr(1);
    hpat = "(?:[a-z0-9-]+\\.)*" + ExtPatternToRegex(suffix);
  } else {
    hpat = ExtPatternToRegex(phost);
  }
  return scheme_re + "://" + hpat + "(?::[0-9]+)?" +
         ExtPatternToRegex(ppath);
}

// Проверка URL по match-pattern (разовая; набор правил компилирует шаблоны
// один раз — см. ExtScriptRules::Prepare).
inline bool ExtUrlMatchesPattern(const std::string& pattern,
                                 const std::string& url) {
  try {
    const std::regex re("^(?:" + ExtUrlPatternToRegex(pattern) + ")$");
    return std::regex_match(ExtNormalizeUrl(url), re);
  } catch (const std::regex_error&) {
    return false;
  }
}

// Проверка include_globs/exclude_globs: `*` — любые символы, `?` — один символ,
// сравнение со всей строкой URL (как в Chromium, регистр учитывается).
inline bool ExtUrlMatchesGlob(const std::string& glob, const std::string& url) {
  if (glob.empty()) return false;
  try {
    const std::regex re("^(?:" + ExtGlobToRegex(glob) + ")$");
    return std::regex_match(url, re);
  } catch (const std::regex_error&) {
    return false;
  }
}

// Полный набор правил одного блока content_scripts из манифеста.
//
// Шаблоны компилируются один раз в Prepare() (после чтения манифеста), а не на
// каждый фрейм и навигацию: с поддержкой all_frames проверок стало больше, и
// компиляция std::regex на каждый вызов занимала десятки микросекунд.
struct ExtScriptRules {
  std::vector<std::string> matches;
  std::vector<std::string> exclude_matches;
  std::vector<std::string> include_globs;
  std::vector<std::string> exclude_globs;
  bool all_frames = false;

  // Компилирует регулярные выражения из строковых шаблонов (вызывается один
  // раз при загрузке манифеста). Списки строк после первого использования
  // менять нельзя — кеш регулярных выражений уже построен.
  void Prepare() { EnsurePrepared(); }

  bool AppliesTo(const std::string& url) const {
    EnsurePrepared();  // если Prepare() не позвали — соберём кеш здесь же
    const std::string target = ExtNormalizeUrl(url);
    bool matched = false;
    for (const auto& re : mre_) {
      if (std::regex_match(target, re)) {
        matched = true;
        break;
      }
    }
    if (!matched) return false;
    for (const auto& re : xre_) {
      if (std::regex_match(target, re)) return false;
    }
    if (!iglo_.empty()) {  // include_globs задаёт обязательное условие
      bool included = false;
      for (const auto& re : iglo_) {
        if (std::regex_match(url, re)) {
          included = true;
          break;
        }
      }
      if (!included) return false;
    }
    for (const auto& re : eglo_) {
      if (std::regex_match(url, re)) return false;
    }
    return true;
  }

  // main_frame получают скрипты всегда, подфреймы — только при all_frames.
  bool AppliesToFrame(bool is_main_frame) const {
    return is_main_frame || all_frames;
  }

 private:
  // Битый шаблон отбрасывается: он не должен ронять вкладку и не должен
  // совпадать. Кеш ленивый, но по факту строится при загрузке манифеста —
  // тогда первая же навигация не платит за компиляцию.
  void EnsurePrepared() const {
    if (prepared_) return;
    mre_.clear();
    xre_.clear();
    iglo_.clear();
    eglo_.clear();
    for (const auto& m : matches) CompilePattern(m, &mre_);
    for (const auto& m : exclude_matches) CompilePattern(m, &xre_);
    for (const auto& g : include_globs) CompileGlob(g, &iglo_);
    for (const auto& g : exclude_globs) CompileGlob(g, &eglo_);
    prepared_ = true;
  }

  static void CompilePattern(const std::string& pattern,
                             std::vector<std::regex>* out) {
    try {
      out->emplace_back("^(?:" + ExtUrlPatternToRegex(pattern) + ")$");
    } catch (const std::regex_error&) {  // некорректный шаблон игнорируем
    }
  }
  static void CompileGlob(const std::string& glob, std::vector<std::regex>* out) {
    try {
      out->emplace_back("^(?:" + ExtGlobToRegex(glob) + ")$");
    } catch (const std::regex_error&) {
    }
  }

  mutable bool prepared_ = false;
  mutable std::vector<std::regex> mre_, xre_, iglo_, eglo_;
};

}  // namespace shelter

#endif  // SHELTER_EXTENSION_MATCH_H_
