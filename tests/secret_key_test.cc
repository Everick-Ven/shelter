// Тест мастер-ключа секретов (src/secret_key.cc, ветка без Keychain/DPAPI).
// Сборка: c++ -std=c++17 -I. tests/secret_key_test.cc src/secret_key.cc
//         src/platform_linux.cc -pthread -o secret-key-test
// Ключ кэшируется в процессе, поэтому каждый сценарий — отдельный запуск этого же
// файла: `secret-key-test <сценарий> <HOME>`; без аргументов работает оркестратор.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "src/platform.h"

namespace fs = std::filesystem;

namespace {

std::string Slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void Put(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << s;
}

// ---- сценарии (выполняются в дочернем процессе) -----------------------------

int RunScenario(const std::string& name) {
  using shelter::platform::SecretKeyHex;
  using shelter::platform::SecretKeyIsPersistent;
  if (name == "key") {
    std::cout << SecretKeyHex() << "\n" << (SecretKeyIsPersistent() ? 1 : 0) << "\n";
    return 0;
  }
  if (name == "threads") {
    std::vector<std::string> out(8);
    std::vector<std::thread> ts;
    for (size_t i = 0; i < out.size(); ++i)
      ts.emplace_back([&out, i] { out[i] = SecretKeyHex(); });
    for (auto& t : ts) t.join();
    std::set<std::string> uniq(out.begin(), out.end());
    std::cout << uniq.size() << "\n" << *uniq.begin() << "\n";
    return 0;
  }
  return 2;
}

// ---- оркестратор ------------------------------------------------------------

int g_fail = 0;
void Check(bool ok, const std::string& what) {
  std::cout << (ok ? "PASS " : "FAIL ") << what << "\n";
  if (!ok) ++g_fail;
}

struct Result {
  std::vector<std::string> lines;
  int status = -1;
};

Result Run(const std::string& self, const std::string& scenario, const fs::path& home) {
  Result r;
  const std::string cmd =
      "HOME='" + home.string() + "' '" + self + "' " + scenario + " 2>/dev/null";
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return r;
  char buf[256];
  std::string cur;
  while (fgets(buf, sizeof buf, p)) {
    cur = buf;
    while (!cur.empty() && (cur.back() == '\n' || cur.back() == '\r')) cur.pop_back();
    r.lines.push_back(cur);
  }
  r.status = pclose(p);
  return r;
}

bool IsHex64(const std::string& s) {
  if (s.size() != 64) return false;
  for (char c : s)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

fs::path FreshHome(const fs::path& base, const std::string& n) {
  fs::path h = base / n;
  fs::remove_all(h);
  fs::create_directories(h);
  return h;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2) return RunScenario(argv[1]);

  const std::string self = fs::canonical("/proc/self/exe").string();
  char tmpl[] = "/tmp/secret-key-test-XXXXXX";
  if (!mkdtemp(tmpl)) return 2;
  const fs::path base = tmpl;
  auto keyfile = [](const fs::path& h) { return h / ".config" / "SHELTER" / "secret.key"; };

  {  // 1. первый запуск: ключ создан и сохранён с правами 0600, без хвостов .tmp
    fs::path h = FreshHome(base, "fresh");
    Result a = Run(self, "key", h);
    Check(a.lines.size() == 2 && IsHex64(a.lines[0]), "первый запуск возвращает 64 hex-символа");
    Check(a.lines.size() == 2 && a.lines[1] == "1", "ключ помечен как сохранённый");
    Check(fs::exists(keyfile(h)) && fs::file_size(keyfile(h)) == 32, "secret.key записан (32 байта)");
    struct stat st{};
    ::stat(keyfile(h).c_str(), &st);
    Check((st.st_mode & 0777) == 0600, "права secret.key равны 0600");
    Check(!fs::exists(fs::path(keyfile(h)).string() + ".tmp"), "временный файл не остался");

    // 2. перезапуск: тот же ключ
    Result b = Run(self, "key", h);
    Check(b.lines.size() == 2 && b.lines[0] == a.lines[0], "после перезапуска ключ тот же");
  }

  {  // 3. повреждённый файл: не затирается, а откладывается в сторону
    fs::path h = FreshHome(base, "corrupt");
    Put(keyfile(h), "junk!");
    Result a = Run(self, "key", h);
    const fs::path un = fs::path(keyfile(h)).string() + ".unreadable";
    Check(a.lines.size() == 2 && IsHex64(a.lines[0]), "при нечитаемом файле создаётся новый ключ");
    Check(fs::exists(un) && Slurp(un) == "junk!", "старый файл сохранён как secret.key.unreadable");
    Check(fs::exists(keyfile(h)) && fs::file_size(keyfile(h)) == 32, "новый secret.key записан");

    // 4. второй сбой не затирает первое сохранённое
    Put(keyfile(h), "other");
    Run(self, "key", h);
    const fs::path un1 = fs::path(keyfile(h)).string() + ".unreadable1";
    Check(Slurp(un) == "junk!" && fs::exists(un1) && Slurp(un1) == "other",
          "повторный сбой пишет в .unreadable1, первое не затирается");
  }

  {  // 5. запись невозможна (на месте временного файла каталог): не молчим об этом
    fs::path h = FreshHome(base, "nowrite");
    fs::create_directories(fs::path(keyfile(h)).string() + ".tmp");
    Result a = Run(self, "key", h);
    Check(a.lines.size() == 2 && IsHex64(a.lines[0]), "без записи ключ всё равно выдан на этот сеанс");
    Check(a.lines.size() == 2 && a.lines[1] == "0", "ключ помечен как несохранённый");
    Check(!fs::exists(keyfile(h)), "частичный secret.key не создан");
  }

  {  // 6. гонка потоков: один ключ
    fs::path h = FreshHome(base, "threads");
    Result a = Run(self, "threads", h);
    Check(a.lines.size() == 2 && a.lines[0] == "1", "8 потоков получили один и тот же ключ");
    Result b = Run(self, "key", h);
    Check(a.lines.size() == 2 && b.lines.size() == 2 && a.lines[1] == b.lines[0],
          "ключ из потоков совпадает с сохранённым");
  }

  fs::remove_all(base);
  std::cout << (g_fail ? "SECRET_KEY_TEST_FAIL" : "SECRET_KEY_TEST_PASS") << "\n";
  return g_fail ? 1 : 0;
}
