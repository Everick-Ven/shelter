// Контракт «тема интерфейса -> цветовая схема Chromium».
//
// Тема в SHELTER — это не только CSS: Chromium должен знать режим (тёмный или
// светлый), иначе страницы видят системную схему, а скроллбары и элементы
// управления рисуются «не из темы». CEF даёт для этого публичный метод
// CefRequestContext::SetChromeColorScheme — он и вызывается из моста
// theme.scheme и при инициализации каждого контекста запросов.
//
// Тест держит две стороны: чистую функцию перевода режима (src/color_scheme.h,
// без CEF) и проводку — shell.cc/shell_bridge.cc должны вызывать именно
// нативный метод, а интерфейс — сообщать тему при её смене.
// Тест не требует CEF (собирается обычным c++, как и остальные).
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "src/color_scheme.h"

namespace {

int Failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << std::endl;
  ++Failures;
}

std::string Read(const char* path) {
  std::ifstream f(path);
  std::stringstream buf;
  buf << f.rdbuf();
  return buf.str();
}

void TestModeMapping() {
  Check(shelter::ColorSchemeDarkFromMode("dark") == true, "dark -> тёмная");
  Check(shelter::ColorSchemeDarkFromMode("light") == false, "light -> светлая");
  // Неизвестное значение не должно осветлять контент: тёмная тема основная.
  Check(shelter::ColorSchemeDarkFromMode("") == true, "пусто -> тёмная");
  Check(shelter::ColorSchemeDarkFromMode("system") == true,
        "неизвестный режим -> тёмная");
  Check(shelter::ColorSchemeDarkFromMode("LIGHT") == true,
        "регистр учитывается: 'LIGHT' не считается светлой");
}

void TestWiring() {
  const std::string shell = Read("src/shell.cc");
  const std::string bridge = Read("src/shell_bridge.cc");
  Check(!shell.empty() && !bridge.empty(), "файлы оболочки читаются");

  // Нативный вызов: оба варианта и оба места (мост + готовность контекста).
  Check(shell.find("SetChromeColorScheme") != std::string::npos,
        "shell.cc сообщает Chromium цветовую схему");
  Check(shell.find("CEF_COLOR_VARIANT_DARK") != std::string::npos &&
            shell.find("CEF_COLOR_VARIANT_LIGHT") != std::string::npos,
        "используются оба режима Chromium");
  Check(shell.find("SetChromeColorScheme(variant, 0)") != std::string::npos &&
            shell.find("for (auto& kv : contexts_)") != std::string::npos,
        "схема применяется ко всем контекстам (у каждого пространства свой)");
  Check(shell.find("CefRequestContext::GetGlobalContext()") != std::string::npos &&
            shell.find("dark_scheme_") != std::string::npos,
        "учитываются глобальный контекст и текущая тема");
  Check(shell.find("OnContextReady(const std::string& partition)") !=
            std::string::npos,
        "готовность контекста обрабатывается в одном месте");
  const auto ready = shell.find("void Shell::OnContextReady(");
  Check(ready != std::string::npos &&
            shell.find("SetChromeColorScheme", ready) != std::string::npos,
        "новый контекст получает тему при инициализации");

  // Мост: тема приходит из UI и применяется и к ОС, и к Chromium.
  const auto handler = bridge.find("if (m == \"theme.scheme\")");
  Check(handler != std::string::npos, "мост принимает theme.scheme");
  if (handler != std::string::npos) {
    const std::string tail = bridge.substr(handler, 400);
    Check(tail.find("ColorSchemeDarkFromMode") != std::string::npos,
          "режим разбирается чистой функцией");
    Check(tail.find("platform::SetColorScheme") != std::string::npos,
          "тема применяется к системе (macOS NSAppearance)");
    Check(tail.find("SetColorSchemeAll") != std::string::npos,
          "тема применяется к Chromium");
  }

  // Интерфейс обязан сообщать тему: и в сборке (host-bridge), и в резервном
  // пути вёрстки (inline NAT), и при применении темы.
  const std::string host = Read("resources/ui/host-bridge.js");
  const std::string html = Read("resources/ui/index.html");
  Check(host.find("themeScheme: function (m) { q('theme.scheme', { mode: m }); }") !=
            std::string::npos,
        "host-bridge отправляет theme.scheme");
  Check(html.find("themeScheme: scheme => cefSend('theme:scheme', { scheme })") !=
            std::string::npos ||
            html.find("themeScheme: m => mq2('theme.scheme'") !=
                std::string::npos,
        "вёрстка имеет резервный путь для темы");
  Check(html.find("shelterNative.themeScheme(") != std::string::npos,
        "тема сообщается при её применении");
}

}  // namespace

int main() {
  TestModeMapping();
  TestWiring();
  if (Failures) {
    std::cerr << Failures << " check(s) failed" << std::endl;
    return 1;
  }
  std::cout << "color_scheme_test: all checks passed" << std::endl;
  return 0;
}
