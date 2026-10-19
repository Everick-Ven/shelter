// Контракт src/sandbox_config.h: рантайм-решение о песочнице обязано совпадать
// с build-конфигурацией на обеих платформах.
//
// CI собирает этот тест трижды:
//   1) -DCEF_USE_SANDBOX   (macOS)              -> expect-on
//   2) -DCEF_USE_BOOTSTRAP (Windows, CEF M138+) -> expect-on
//   3) без макросов        (сборка без песочницы) -> expect-off
// Аргумент — ожидаемое значение SandboxCompiledIn(). Случай (2) — регрессия
// на реальный инцидент: no_sandbox=true на Windows при зелёном CI-гейте.
#include <iostream>
#include <string>

#include "src/sandbox_config.h"

int main(int argc, char** argv) {
  if (argc != 2 ||
      (std::string(argv[1]) != "expect-on" && std::string(argv[1]) != "expect-off")) {
    std::cerr << "usage: sandbox_config_test expect-on|expect-off" << std::endl;
    return 2;
  }
  const bool expected = std::string(argv[1]) == "expect-on";
  const bool got = shelter::SandboxCompiledIn();
  if (got != expected) {
    std::cerr << "FAIL: SandboxCompiledIn() = " << (got ? "true" : "false")
              << ", ожидалось " << (expected ? "true" : "false") << std::endl;
    return 1;
  }
  std::cout << "sandbox-config-test " << argv[1] << ": OK" << std::endl;
  return 0;
}
