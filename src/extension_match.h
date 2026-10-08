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

// Проверка URL по match-pattern: `<all_urls>`, `*://<host>/<path>`,
// `<scheme>://*.example.com/<path>`. Хост `*` или `*.example.com` разворачивается
// как в Chromium (любой хост / хост и его поддомены).
inline bool ExtUrlMatchesPattern(const std::string& pattern,
                                 const std::string& url) {
  std::string scheme, host, path;
  if (!ExtSplitUrl(url, &scheme, &host, &path)) return false;
  if (pattern == "<all_urls>") return scheme == "http" || scheme == "https";
  const auto sep = pattern.find("://");
  if (sep == std::string::npos || sep == 0) return false;
  std::string pscheme = pattern.substr(0, sep);
  for (char& c : pscheme)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const std::string rest = pattern.substr(sep + 3);
  const auto slash = rest.find('/');
  std::string phost = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string ppath = slash == std::string::npos ? "/" : rest.substr(slash);
  if (ppath.empty()) ppath = "/";
  if (phost.empty() || phost.find(':') != std::string::npos) return false;
  for (char& c : phost)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  // `*` в схеме — только http/https (как в Chromium), не любой протокол.
  if (pscheme == "*") {
    if (scheme != "http" && scheme != "https") return false;
  } else if (pscheme != scheme) {
    return false;
  }
  std::string hpat;
  if (phost == "*") {
    hpat = ".*";
  } else if (phost[0] == '*') {  // *.example.com — домен и его поддомены
    std::string suffix = phost.substr(1);
    if (!suffix.empty() && suffix[0] == '.') suffix = suffix.substr(1);
    hpat = "([a-z0-9.-]+\\.)?" + ExtPatternToRegex(suffix);
  } else {
    hpat = ExtPatternToRegex(phost);
  }
  if (!std::regex_match(host, std::regex(hpat))) return false;
  return std::regex_match(path, std::regex(ExtPatternToRegex(ppath)));
}

// Проверка include_globs/exclude_globs: `*` — любые символы, `?` — один символ,
// сравнение со всей строкой URL (как в Chromium, регистр учитывается).
inline bool ExtUrlMatchesGlob(const std::string& glob, const std::string& url) {
  if (glob.empty()) return false;
  std::string re;
  re.reserve(glob.size() + 8);
  for (const char c : glob) {
    if (c == '*') {
      re += ".*";
    } else if (c == '?') {
      re += ".";
    } else if (std::strchr(".+?[]^$(){}|\\", c) != nullptr) {
      re += '\\';
      re += c;
    } else {
      re += c;
    }
  }
  return std::regex_match(url, std::regex(re));
}

// Полный набор правил одного блока content_scripts из манифеста.
struct ExtScriptRules {
  std::vector<std::string> matches;
  std::vector<std::string> exclude_matches;
  std::vector<std::string> include_globs;
  std::vector<std::string> exclude_globs;
  bool all_frames = false;

  bool AppliesTo(const std::string& url) const {
    bool matched = false;
    for (const auto& m : matches) {
      if (ExtUrlMatchesPattern(m, url)) {
        matched = true;
        break;
      }
    }
    if (!matched) return false;
    for (const auto& m : exclude_matches) {
      if (ExtUrlMatchesPattern(m, url)) return false;
    }
    if (!include_globs.empty()) {
      bool included = false;
      for (const auto& g : include_globs) {
        if (ExtUrlMatchesGlob(g, url)) {
          included = true;
          break;
        }
      }
      if (!included) return false;
    }
    for (const auto& g : exclude_globs) {
      if (ExtUrlMatchesGlob(g, url)) return false;
    }
    return true;
  }

  // main_frame получают скрипты всегда, подфреймы — только при all_frames.
  bool AppliesToFrame(bool is_main_frame) const {
    return is_main_frame || all_frames;
  }
};

}  // namespace shelter

#endif  // SHELTER_EXTENSION_MATCH_H_
