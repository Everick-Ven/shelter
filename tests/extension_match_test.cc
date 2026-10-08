// Контракт правил совпадения content_scripts расширений.
//
// Оболочка сама внедряет content-scripts из пакета (CEF вырезал API расширений),
// поэтому «какие фреймы и на каких страницах получают скрипт» решает код в
// src/extension_match.h. Здесь закреплена семантика Chromium: matches как
// обязательное условие, exclude_matches и exclude_globs как запрет,
// include_globs как дополнительный фильтр, all_frames как разрешение подфреймов.
// Тест не требует CEF — компилируется обычным c++ (см. шаг в build.yml).
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "src/extension_match.h"

namespace {

int Failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << std::endl;
  ++Failures;
}

shelter::ExtScriptRules Rules(std::vector<std::string> matches,
                              std::vector<std::string> exclude_matches = {},
                              std::vector<std::string> include_globs = {},
                              std::vector<std::string> exclude_globs = {},
                              bool all_frames = false) {
  shelter::ExtScriptRules r;
  r.matches = std::move(matches);
  r.exclude_matches = std::move(exclude_matches);
  r.include_globs = std::move(include_globs);
  r.exclude_globs = std::move(exclude_globs);
  r.all_frames = all_frames;
  r.Prepare();
  return r;
}

}  // namespace

int main() {
  using shelter::ExtScriptRules;
  using shelter::ExtSplitUrl;
  using shelter::ExtUrlMatchesGlob;
  using shelter::ExtUrlMatchesPattern;

  // --- разбор URL -----------------------------------------------------------
  std::string scheme, host, path;
  Check(ExtSplitUrl("https://Example.COM/Path?a=1#frag", &scheme, &host, &path),
        "https URL разобран");
  Check(scheme == "https" && host == "example.com" && path == "/Path",
        "схема и хост в нижнем регистре, query/fragment отброшены");
  Check(ExtSplitUrl("http://example.com", &scheme, &host, &path) && path == "/",
        "пустой путь превращается в /");
  Check(!ExtSplitUrl("about:blank", &scheme, &host, &path),
        "about:blank — не HTTP-страница");
  Check(!ExtSplitUrl("data:text/html,hi", &scheme, &host, &path),
        "data: — не HTTP-страница");

  // --- matches: базовые шаблоны --------------------------------------------
  Check(ExtUrlMatchesPattern("<all_urls>", "https://example.com/a"),
        "<all_urls> ловит https");
  Check(ExtUrlMatchesPattern("<all_urls>", "http://example.com/a"),
        "<all_urls> ловит http");
  Check(!ExtUrlMatchesPattern("<all_urls>", "file:///etc/hosts"),
        "<all_urls> не трогает local files");
  Check(ExtUrlMatchesPattern("*://example.com/*", "http://example.com/x"),
        "* в схеме совпадает с http");
  Check(ExtUrlMatchesPattern("*://example.com/*", "https://example.com"),
        "* в схеме совпадает с https, пустой путь ок");
  Check(!ExtUrlMatchesPattern("*://example.com/*", "https://other.example.com/x"),
        "точный хост не включает поддомены");
  Check(ExtUrlMatchesPattern("*://*.example.com/*", "https://example.com/"),
        "*.host совпадает с самим доменом");
  Check(ExtUrlMatchesPattern("*://*.example.com/*", "https://a.b.example.com/"),
        "*.host совпадает с поддоменами");
  Check(!ExtUrlMatchesPattern("*://*.example.com/*", "https://example.com.evil.net/"),
        "поддомен-шаблон не протекает в чужой домен");
  Check(ExtUrlMatchesPattern("*://*/*", "https://anything.test/whatever"),
        "хост * совпадает с любым");
  Check(ExtUrlMatchesPattern("https://example.com/api/*", "https://example.com/api/v1/x"),
        "префикс пути с *");
  Check(!ExtUrlMatchesPattern("https://example.com/api/*", "https://example.com/app/x"),
        "префикс пути не совпадает с другим путём");
  Check(ExtUrlMatchesPattern("https://example.com/api/*", "http://example.com/api/x") == false,
        "явная схема не подменяется другой");
  Check(!ExtUrlMatchesPattern("example.com", "https://example.com/"),
        "шаблон без схемы не считается");
  Check(ExtUrlMatchesPattern("*://example.com/*", "https://EXAMPLE.com/x"),
        "хост сравнивается без учёта регистра");
  Check(ExtUrlMatchesPattern("https://example.com/api", "https://example.com/api?x=1#top"),
        "query/fragment не мешают точному пути");
  Check(!ExtUrlMatchesPattern("https://example.com/api", "https://example.com/api/v2"),
        "точный путь не растягивается на подпапки");
  Check(Rules({"*://*/*"}).AppliesTo("https://example.com/api?x=1"),
        "правила совпадают и при query в URL");
  Check(!Rules({"*://*/api"}, {}, {}, {}, false).AppliesTo("https://example.com/other?a=/api"),
        "query не подменяет путь для правил");
  Check(!ExtUrlMatchesPattern("*://example.com/*", "ftp://example.com/x"),
        "схема * не пропускает чужой протокол");
  Check(ExtUrlMatchesPattern("*://example.com/*", "https://example.com:8443/x"),
        "порт в URL не мешает совпадению хоста");
  Check(!ExtUrlMatchesPattern("*://example.com:8443/*", "https://example.com:8443/x"),
        "шаблон с портом невалиден (в Chrome портов в шаблонах нет)");
  Check(!ExtUrlMatchesPattern("*://example.com/*", "https://example.com.evil.net/x"),
        "шаблон хоста не пропускает домен-обманку");

  // --- exclude_matches ------------------------------------------------------
  Check(Rules({"*://*/*"}, {"*://mail.example.com/*"})
            .AppliesTo("https://example.com/"),
        "exclude_matches не мешает другим страницам");
  Check(!Rules({"*://*/*"}, {"*://mail.example.com/*"})
             .AppliesTo("https://mail.example.com/inbox"),
        "exclude_matches выключает скрипт на своей странице");
  Check(!Rules({"*://mail.example.com/*"}, {"*://mail.example.com/*"})
             .AppliesTo("https://mail.example.com/"),
        "пустое пересечение matches/exclude_matches = нет внедрения");

  // --- globs ----------------------------------------------------------------
  Check(ExtUrlMatchesGlob("https://example.com/*", "https://example.com/a/b"),
        "glob * через слэши");
  Check(ExtUrlMatchesGlob("https://example.com/??", "https://example.com/ab"),
        "glob ? — ровно один символ");
  Check(!ExtUrlMatchesGlob("https://example.com/??", "https://example.com/abc"),
        "glob ? не растягивается");
  Check(ExtUrlMatchesGlob("*.example.com/*", "https://a.example.com/x"),
        "glob по подстроке хоста");
  Check(!ExtUrlMatchesGlob("https://example.com/*", "https://example.com.evil.net/x"),
        "glob привязан к полной строке");
  Check(Rules({"*://*/*"}, {}, {"*://*/cart*"})
            .AppliesTo("https://shop.test/cart/1"),
        "include_globs пропускает совпавший URL");
  Check(!Rules({"*://*/*"}, {}, {"*://*/cart*"})
             .AppliesTo("https://shop.test/catalog"),
        "include_globs блокирует несовпавший URL");
  Check(Rules({"*://*/*"}, {}, {}, {"*://*/logout*"})
            .AppliesTo("https://shop.test/account"),
        "exclude_globs не мешает другим URL");
  Check(!Rules({"*://*/*"}, {}, {}, {"*://*/logout*"})
             .AppliesTo("https://shop.test/logout?next=1"),
        "exclude_globs выключает скрипт");
  Check(Rules({"*://*/*"}, {"*://example.com/*"}, {"*://*/x"}, {"*://*/y"})
            .AppliesTo("https://other.test/x"),
        "комбинация правил: matches + include_globs");
  Check(!Rules({"*://*/*"}, {}, {}, {}).AppliesTo("https://other.test/x") == false,
        "пустой matches — никогда (пустой список = нет внедрения)");

  // --- all_frames -----------------------------------------------------------
  Check(Rules({"*://*/*"}).AppliesToFrame(true),
        "главный фрейм получает скрипт всегда");
  Check(!Rules({"*://*/*"}).AppliesToFrame(false),
        "без all_frames подфрейм не получает скрипт");
  Check(Rules({"*://*/*"}, {}, {}, {}, true).AppliesToFrame(false),
        "all_frames разрешает подфреймы");
  Check(Rules({"*://*/*"}, {}, {}, {}, true).AppliesToFrame(true),
        "all_frames не ломает главный фрейм");

  // Границы: пустые правила и пустые строки.
  Check(!ExtUrlMatchesGlob("", "https://example.com/"), "пустой glob не совпадает");
  Check(!ExtUrlMatchesPattern("", "https://example.com/"), "пустой шаблон не совпадает");
  Check(ExtScriptRules().AppliesTo("https://example.com/") == false,
        "правила по умолчанию никого не затрагивают");

  // Неподготовленный набор считается тем же способом (AppliesTo готовит копию).
  {
    shelter::ExtScriptRules raw;
    raw.matches = {"*://*.example.com/*"};
    Check(!raw.AppliesTo("https://other.test/"), "неподготовленные правила: мимо");
    Check(raw.AppliesTo("https://a.example.com/x"), "неподготовленные правила: совпадение");
  }

  // Битый шаблон не совпадает и не роняет проверку.
  {
    shelter::ExtScriptRules bad;
    bad.matches = {"*://[broken/*"};
    bad.Prepare();
    Check(!bad.AppliesTo("https://example.com/"), "битый шаблон просто не совпадает");
  }

  // --- проводка в оболочке --------------------------------------------------
  // Правила бесполезны, если shell.cc снова начнёт внедрять скрипты только в
  // главный фрейм или забудет про exclude/include-поля манифеста.
  {
    std::ifstream shell("src/shell.cc");
    std::stringstream buf;
    buf << shell.rdbuf();
    const std::string src = buf.str();
    Check(!src.empty(), "src/shell.cc читается для проверки проводки");
    Check(src.find("#include \"src/extension_match.h\"") != std::string::npos,
          "shell.cc подключает модуль правил");
    Check(src.find("cs.rules.AppliesTo(url)") != std::string::npos,
          "shell.cc фильтрует фреймы правилами (matches/globs)");
    Check(src.find("cs.rules.AppliesToFrame(is_main_frame)") != std::string::npos,
          "shell.cc различает главный фрейм и подфреймы (all_frames)");
    const auto inject_begin = src.find("void InjectExtScripts(");
    const auto inject_end = src.find("// Загрузка .crx/.zip пакета расширения");
    const bool inject_found =
        inject_begin != std::string::npos && inject_end > inject_begin;
    Check(inject_found, "найдена функция внедрения content-scripts");
    if (inject_found) {
      const std::string inject =
          src.substr(inject_begin, inject_end - inject_begin);
      Check(inject.find("IsMain()") == std::string::npos ||
                inject.find("is_main_frame") != std::string::npos,
            "внедрение не отсекает подфреймы целиком");
      Check(inject.find("rules.AppliesToFrame") != std::string::npos,
            "решение о фрейме принимают правила");
    }
    for (const char* key : {"\"exclude_matches\"", "\"include_globs\"",
                            "\"exclude_globs\"", "\"all_frames\""}) {
      Check(src.find(key) != std::string::npos,
            "shell.cc читает поле манифеста из rules-набора");
    }
  }

  if (Failures != 0) {
    std::cerr << Failures << " check(s) failed" << std::endl;
    return 1;
  }
  std::cout << "extension_match_test: all checks passed" << std::endl;
  return 0;
}
