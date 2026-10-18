// Контракт загрузки расширений в движок Chromium.
//
// CEF вырезал embedder-API расширений, поэтому единственный оставшийся путь —
// штатный аргумент Chromium `--load-extension`, который читает
// ExtensionService при инициализации профиля. Значение аргумента собирает
// src/extension_engine.h; здесь закреплены: состав списка (MV3 и только
// существующие каталоги — проверяется на стороне shell.cc), безопасность
// путей (запятая в значении — разделитель), лимит и разбор файла режима
// «движок/оболочка», от которого зависит и аргумент, и ручное внедрение.
// Тест не требует CEF — компилируется обычным c++ (см. шаг в build.yml).
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "src/extension_engine.h"

namespace {

int Failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << std::endl;
  ++Failures;
}

std::string JoinDirs(const std::vector<std::string>& v) {
  std::string out;
  for (const auto& s : v) {
    if (!out.empty()) out.push_back(',');
    out += s;
  }
  return out;
}

std::string TempDir() {
  std::string dir = "/tmp/shelter_ext_engine_test";
  std::string nested = dir + "/Default/Extensions";
  std::string cmd = "rm -rf '" + dir + "' && mkdir -p '" + nested + "'";
  if (std::system(cmd.c_str()) != 0) return std::string();
  return dir;
}

void TestSwitchValue() {
  Check(shelter::BuildLoadExtensionValue({}) == "", "пустой список -> пустое значение");
  Check(shelter::BuildLoadExtensionValue({"/a/b", "/c/d"}) == "/a/b,/c/d",
        "пути соединяются запятыми");

  // Запятая в пути разорвала бы значение на два пути — такой каталог в движок
  // не отдаём.
  std::vector<std::string> skipped;
  const std::string v = shelter::BuildLoadExtensionValue(
      {"/ok/one", "/bad,comma", "/ok/two"}, shelter::kMaxEngineExtensions,
      &skipped);
  Check(v == "/ok/one,/ok/two", "путь с запятой пропускается");
  Check(skipped.size() == 1 && skipped[0] == "/bad,comma",
        "пропущенный путь попадает в отчёт");

  Check(!shelter::ExtEnginePathSafe(""), "пустой путь не годится");
  Check(!shelter::ExtEnginePathSafe("/x\"y"), "кавычка в пути не годится");
  Check(!shelter::ExtEnginePathSafe("/x\ny"), "перевод строки в пути не годится");
  Check(shelter::ExtEnginePathSafe("/Users/u/Library/Application Support/x"),
        "пробелы в пути допустимы");

  // Лимит применяется и к переданным, «лишние» уходят в skipped.
  std::vector<std::string> many;
  for (int i = 0; i < 40; ++i) many.push_back("/e/" + std::to_string(i));
  std::vector<std::string> over;
  const std::string capped =
      shelter::BuildLoadExtensionValue(many, 32, &over);
  size_t commas = 0;
  for (char c : capped) {
    if (c == ',') ++commas;
  }
  Check(commas == 31, "лимит 32 пути -> 32 элемента и 31 запятая");
  Check(over.size() == 8, "остальные пути попадают в skipped");
}

void TestModeFile() {
  Check(shelter::ExtEngineEnabledByText("") == true, "пустой файл -> движок");
  Check(shelter::ExtEngineEnabledByText("engine\n") == true, "engine -> движок");
  Check(shelter::ExtEngineEnabledByText(" shell \n") == false,
        "shell (с пробелами) -> оболочка");
  Check(shelter::ExtEngineEnabledByText("мусор") == true, "незнакомое -> движок");
  Check(shelter::ExtEngineModeText(false) == "shell\n",
        "запись режима оболочки");
  Check(shelter::ExtEngineModeText(true) == "engine\n", "запись режима движка");

  const std::string dir = TempDir();
  if (dir.empty()) {
    Check(false, "не удалось создать временный каталог");
    return;
  }
  // Без файла режим по умолчанию — движок.
  Check(shelter::ExtEngineEnabled(dir) == true, "по умолчанию движок");
  Check(shelter::ExtEngineSetEnabled(dir, false), "режим оболочки записывается");
  Check(shelter::ExtEngineEnabled(dir) == false, "режим оболочки читается");
  Check(shelter::ExtEngineSetEnabled(dir, true), "режим движка записывается");
  Check(shelter::ExtEngineEnabled(dir) == true, "режим движка читается");
  std::remove(shelter::ExtModeFile(dir).c_str());
}

void TestManifestVersion() {
  Check(shelter::ExtManifestVersionFromText("{\"manifest_version\": 3}") == 3,
        "mv3 читается");
  Check(shelter::ExtManifestVersionFromText("{\"manifest_version\":2 }") == 2,
        "mv2 без пробела после двоеточия читается");
  Check(shelter::ExtManifestVersionFromText("{}") == 1,
        "манифест без версии считается v1");
  Check(shelter::ExtManifestVersionFromText(
            "{\"description\":\"manifest_version is not here\","
            "\"manifest_version\": 3}") == 3,
        "ключ в описании не мешает найти настоящий manifest_version");
  Check(shelter::ExtManifestVersionFromText("{\"manifest_version\": \"3\"}") == 1,
        "строка вместо числа не считается версией");
}

void TestCollectDirs() {
  const std::string base = "/tmp/shelter_ext_engine_collect";
  const std::string root_a = base + "/Default/Extensions";
  const std::string root_b = base + "/Extensions";
  const std::string cmd =
      "rm -rf '" + base + "' && mkdir -p '" + root_a + "/mv3id' '" + root_a +
      "/mv2id' '" + root_a + "/_tmp123' '" + root_a + "/bad id' '" + root_b +
      "/mv3id' '" + root_b + "/other3'"
      " && printf '{\"manifest_version\": 3}' > '" + root_a + "/mv3id/manifest.json'"
      " && printf '{\"manifest_version\": 2}' > '" + root_a + "/mv2id/manifest.json'"
      " && printf '{\"manifest_version\": 3}' > '" + root_a + "/_tmp123/manifest.json'"
      " && printf '{\"manifest_version\": 3}' > '" + root_a + "/bad id/manifest.json'"
      " && printf '{\"manifest_version\": 3}' > '" + root_b + "/mv3id/manifest.json'"
      " && printf '{\"manifest_version\": 3}' > '" + root_b + "/other3/manifest.json'"
      " && mkdir -p '" + root_a + "/noversion'"
      " && printf '{}' > '" + root_a + "/noversion/manifest.json'";
  if (std::system(cmd.c_str()) != 0) {
    Check(false, "не удалось подготовить каталоги расширений");
    return;
  }

  const auto dirs = shelter::ExtEngineCollectDirs({root_a, root_b}, 32);
  Check(dirs.size() == 2, "в движок идут только MV3 из первого корня плюс новый id");
  Check(JoinDirs(dirs) == root_a + "/mv3id," + root_b + "/other3",
        "порядок и состав каталогов");
  Check(shelter::ExtDirIsEngineLoadable(root_a + "/mv2id") == false,
        "MV2 движку не отдаётся");
  Check(shelter::ExtDirIsEngineLoadable(root_a + "/bad id") == true,
        "каталог с пробелом читается (аргумент передаётся без оболочки)");
  Check(shelter::ExtDirIsEngineLoadable(root_a + "/noversion") == false,
        "манифест без версии не отдаётся");

  const auto capped = shelter::ExtEngineCollectDirs({root_a, root_b}, 1);
  Check(capped.size() == 1, "лимит применяется при сборе");

  const std::string value = shelter::BuildLoadExtensionValue(dirs);
  Check(value.find(',', 0) != std::string::npos,
        "два каталога соединяются запятой в аргументе");
}

void TestPassedIds() {
  Check(shelter::ExtDirName("/a/b/c") == "c", "имя каталога из posix-пути");
  Check(shelter::ExtDirName("C:\\data\\ext\\id1") == "id1", "имя каталога из win-пути");
  Check(shelter::ExtDirName("/a/b/") == "b", "хвостовой разделитель не мешает");
  Check(shelter::ExtDirName("") == "", "пустой путь -> пустое имя");

  auto& passed = shelter::ExtEnginePassed();
  passed.clear();
  passed.push_back("/data/Default/Extensions/id1");
  passed.push_back("C:\\data\\Extensions\\id2");
  const auto ids = shelter::ExtEnginePassedIds();
  Check(ids.size() == 2 && ids[0] == "id1" && ids[1] == "id2",
        "переданные движку id читаются из путей обеих раскладок");
  passed.clear();
}

void TestRootCandidates() {
  Check(shelter::ExtRootCandidates("").empty(), "без каталога данных кандидатов нет");
  const auto v = shelter::ExtRootCandidates("/data/shelter");
  Check(v.size() == 2, "два кандидата раскладки");
  Check(v[0] == "/data/shelter/Default/Extensions",
        "основной кандидат — каталог профиля Default");
  Check(v[1] == "/data/shelter/Extensions", "запасной кандидат — каталог данных");
}

// Проводка: заголовок бесполезен, если shell.cc/browser_app.cc его не
// используют или используют неверно (двойное внедрение, аргумент после старта).
void TestWiring() {
  std::ifstream shell_file("src/shell.cc");
  std::stringstream shell_buf;
  shell_buf << shell_file.rdbuf();
  const std::string shell = shell_buf.str();
  Check(!shell.empty(), "src/shell.cc читается");
  Check(shell.find("#include \"src/extension_engine.h\"") != std::string::npos,
        "shell.cc подключает модуль режима расширений");
  Check(shell.find("ExtEngineModeOn()") != std::string::npos,
        "shell.cc знает режим расширений");
  Check(shell.find("e.mv = d->GetInt(\"manifest_version\")") != std::string::npos,
        "shell.cc читает manifest_version");
  Check(shell.find("e.engine = ExtEngineModeOn() && e.mv >= 3") != std::string::npos,
        "движку отдаются только manifest v3+");

  const auto inject_begin = shell.find("void InjectExtScripts(");
  const auto inject_end = shell.find("// Загрузка .crx/.zip пакета расширения");
  const bool inject_found =
      inject_begin != std::string::npos && inject_end > inject_begin;
  Check(inject_found, "найдена функция внедрения content-scripts");
  if (inject_found) {
    const std::string inject =
        shell.substr(inject_begin, inject_end - inject_begin);
    Check(inject.find("kv.second.engine && kv.second.passed") != std::string::npos,
          "движковое расширение не внедряется повторно, но ручное внедрение "
          "работает до перезапуска");
  }
  // Проверка факта загрузки: оболочка спрашивает у движка само расширение.
  Check(shell.find("chrome-extension://") != std::string::npos &&
            shell.find("/manifest.json") != std::string::npos,
        "shell.cc проверяет загрузку запросом к chrome-extension://…/manifest.json");
  Check(shell.find("ExtStartEngineProbes()") != std::string::npos &&
            shell.find("if (!e.engine || !e.passed) continue;") != std::string::npos,
        "проверяются только расширения, переданные движку в этом запуске");
  Check(shell.find("\\\"probe\\\"") != std::string::npos &&
            shell.find("probe->second == 2 ? \"ok\" : \"fail\"") !=
                std::string::npos,
        "состояние проверки уходит в интерфейс (ok/pending/fail)");
  Check(shell.find("kv.second.engine && kv.second.passed") != std::string::npos &&
            shell.find("probe->second == 2") != std::string::npos,
        "«загружено» подтверждается ответом движка, а не аргументом");

  // Установленное в этой сессии расширение обязано быть видно: каталоги
  // сканируются по обеим раскладкам (та же логика, что в --load-extension).
  Check(shell.find("ExtScanRoots()") != std::string::npos &&
            shell.find("ExtRootCandidates(platform::UserDataDir())") !=
                std::string::npos,
        "сканирование расширений идёт по тем же корням, что аргумент движка");

  std::ifstream app_file("src/browser_app.cc");
  std::stringstream app_buf;
  app_buf << app_file.rdbuf();
  const std::string app = app_buf.str();
  Check(!app.empty(), "src/browser_app.cc читается");
  Check(app.find("#include \"src/extension_engine.h\"") != std::string::npos,
        "browser_app.cc подключает модуль режима расширений");
  Check(app.find("ExtEngineCollectDirs(") != std::string::npos &&
            app.find("AppendSwitchWithValue(\"load-extension\"") !=
                std::string::npos,
        "browser_app.cc передаёт каталоги движку аргументом --load-extension");
  Check(app.find("ExtEnginePassed() = dirs") != std::string::npos,
        "переданные движку каталоги запоминаются для честного статуса");
  Check(app.find("ExtEngineEnabled(root)") != std::string::npos,
        "режим «только оболочка» отключает аргумент");
  const auto switch_pos = app.find("load-extension");
  const auto net_pos = app.find("disable-background-networking");
  Check(net_pos == std::string::npos || switch_pos < net_pos,
        "аргумент ставится в OnBeforeCommandLineProcessing до старта CEF");
}

}  // namespace

int main() {
  TestSwitchValue();
  TestModeFile();
  TestRootCandidates();
  TestPassedIds();
  TestManifestVersion();
  TestCollectDirs();
  TestWiring();
  if (Failures) {
    std::cerr << Failures << " check(s) failed" << std::endl;
    return 1;
  }
  std::cout << "extension_engine_test: all checks passed" << std::endl;
  return 0;
}
