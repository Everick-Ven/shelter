// Правила совпадения для content_scripts расширений (match-patterns и globs).
//
// CEF вырезал публичный API расширений (~M127), поэтому оболочка сама
// выполняет ту часть модели Chromium-расширений, которая не требует Chrome-UI:
// объявленные в манифесте content_scripts (JS/CSS) внедряются в подходящие
// фреймы вкладок, а расширения с manifest v3 отдаются движку Chromium
// аргументом --load-extension (см. src/extension_engine.h). Чтобы поведение
// совпадало с Chromium, разбор `matches` / `exclude_matches` / `include_globs` /
// `exclude_globs` / `all_frames` живёт здесь и покрыт обычным c++ тестом
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
//
// Сопоставление реализовано вручную (без std::regex): сборка Chromium идёт с
// выключенными исключениями, а std::regex может бросить исключение на
// некорректном шаблоне или на сложном совпадении. Ручной сопоставитель не
// бросает ничего, не зависит от состояния регулярок и работает быстрее.
#ifndef SHELTER_EXTENSION_MATCH_H_
#define SHELTER_EXTENSION_MATCH_H_

#include <cctype>
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

// Совпадение строки с шаблоном, где `*` — любая последовательность символов,
// `?` — один символ (как в globs Chromium). Итеративный алгоритм с одной
// запомненной звёздочкой: без рекурсии, без исключений, без катастрофического
// отката. `*` совпадает и с `/`: в шаблонах Chrome он покрывает любые символы.
inline bool ExtWildMatch(const std::string& pattern, const std::string& text) {
  size_t p = 0, t = 0;
  size_t star = std::string::npos, mark = 0;
  while (t < text.size()) {
    if (p < pattern.size() &&
        (pattern[p] == '?' || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = t;
    } else if (star != std::string::npos) {
      p = star + 1;
      t = ++mark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

// Разбор match-pattern на части. Возвращает false, если шаблон невалиден:
// нет схемы, пустой хост, порт в хосте, `*` в середине схемы или хоста.
// (Chromium отвергает такие шаблоны при разборе манифеста.)
inline bool ExtParsePattern(const std::string& pattern, std::string* scheme,
                            std::string* host, std::string* path) {
  if (pattern.empty()) return false;
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
  if (phost.find('*') != std::string::npos) {
    // Разрешены только `*` и `*.домен` (как в Chromium).
    if (phost != "*" &&
        (phost.rfind("*.", 0) != 0 || phost.find('*', 2) != std::string::npos)) {
      return false;
    }
  }
  // Хост — доменное имя (буквы, цифры, дефис, точки), без пустых меток.
  // Chromium отвергает такие шаблоны при разборе манифеста, поэтому «совпасть
  // случайно» они не могут.
  {
    const std::string h = phost == "*" ? std::string() : phost;
    const size_t begin = h.rfind("*.", 0) == 0 ? 2 : 0;
    if (begin == 2 && h.size() == 2) return false;
    bool prev_dot = false;
    for (size_t i = begin; i < h.size(); ++i) {
      const char c = h[i];
      const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
      if (c == '.') {
        if (prev_dot || i == begin || i + 1 == h.size()) return false;
        prev_dot = true;
        continue;
      }
      if (!alnum && c != '-') return false;
      prev_dot = false;
    }
  }
  // В схеме `*` допустим только целиком: ни `ht*`, ни `h*ps`.
  if (pscheme.find('*') != std::string::npos && pscheme != "*") return false;
  if (pscheme.empty()) return false;
  if (ppath[0] != '/') return false;
  *scheme = std::move(pscheme);
  *host = std::move(phost);
  *path = std::move(ppath);
  return true;
}

// Проверка URL по match-pattern.
inline bool ExtUrlMatchesPattern(const std::string& pattern,
                                 const std::string& url) {
  std::string uscheme, uhost, upath;
  if (!ExtSplitUrl(url, &uscheme, &uhost, &upath)) return false;

  if (pattern == "<all_urls>")
    return uscheme == "http" || uscheme == "https";

  std::string pscheme, phost, ppath;
  if (!ExtParsePattern(pattern, &pscheme, &phost, &ppath)) return false;
  // `*` в схеме — только http/https (как в Chromium), не любой протокол.
  if (pscheme == "*") {
    if (uscheme != "http" && uscheme != "https") return false;
  } else if (pscheme != uscheme) {
    return false;
  }
  if (phost == "*") {
    // любой хост
  } else if (phost.rfind("*.", 0) == 0) {
    // *.example.com — сам домен и его поддомены, но не «example.com.evil.net».
    const std::string suffix = phost.substr(2);
    if (uhost.size() < suffix.size()) return false;
    if (uhost.compare(uhost.size() - suffix.size(), suffix.size(), suffix) != 0)
      return false;
    if (uhost.size() != suffix.size() &&
        uhost[uhost.size() - suffix.size() - 1] != '.') {
      return false;
    }
  } else if (uhost != phost) {
    return false;
  }
  return ExtWildMatch(ppath, upath);
}

// Проверка include_globs/exclude_globs: `*` — любые символы, `?` — один символ,
// сравнение со всей строкой URL (как в Chromium, регистр учитывается).
inline bool ExtUrlMatchesGlob(const std::string& glob, const std::string& url) {
  if (glob.empty()) return false;
  return ExtWildMatch(glob, url);
}

// Полный набор правил одного блока content_scripts из манифеста.
struct ExtScriptRules {
  std::vector<std::string> matches;
  std::vector<std::string> exclude_matches;
  std::vector<std::string> include_globs;
  std::vector<std::string> exclude_globs;
  bool all_frames = false;

  // Проверка чистая и без состояния: невалидный шаблон просто не совпадает,
  // поэтому предварительная компиляция (и её ошибки) не нужны.
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
    if (!include_globs.empty()) {  // include_globs задаёт обязательное условие
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
