// Контракт кодирования тумблеров защит для командной строки дочерних процессов.
//
// Браузерный процесс пишет switch, рендерер его читает. Если формат разъедется
// (переименование, другой разделитель, потеря поля), защиты молча вернутся к
// значениям по умолчанию на любом новом процессе — то есть на каждой странице
// другого сайта. Тест держит контракт: обе стороны описаны одной парой функций.
#include <cassert>
#include <iostream>
#include <string>

#include "src/protection_flags.h"

namespace {

int Failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << std::endl;
  ++Failures;
}

}  // namespace

int main() {
  using shelter::kProtectionSwitch;
  using shelter::ParseProtectionSwitch;
  using shelter::ProtectionSwitchValue;

  Check(std::string(kProtectionSwitch) == "shelter-protections",
        "switch name is stable");

  // Круговой прогон всех четырёх сочетаний тумблеров.
  for (int flags = 0; flags < 4; ++flags) {
    bool fp = !(flags & 1);
    bool cookies = !(flags & 2);
    const std::string value = ProtectionSwitchValue(flags);
    Check(ParseProtectionSwitch(value, &fp, &cookies),
          "own encoding parses back");
    Check(fp == ((flags & 1) != 0), "anti-fingerprint flag survives round trip");
    Check(cookies == ((flags & 2) != 0),
          "cookie auto-consent flag survives round trip");
  }

  // Значения по умолчанию не должны меняться на чужой строке.
  {
    bool fp = true;
    bool cookies = true;
    Check(!ParseProtectionSwitch("", &fp, &cookies), "empty value is rejected");
    Check(!ParseProtectionSwitch("something-else", &fp, &cookies),
          "foreign value is rejected");
    Check(!ParseProtectionSwitch("fp=1", &fp, &cookies),
          "partial value is rejected");
    Check(fp && cookies, "rejected values leave the defaults untouched");
  }

  // Явные строки: читаем именно своё поле, а не «единицу где-то рядом».
  {
    bool fp = false;
    bool cookies = false;
    Check(ParseProtectionSwitch("fp=1,cookies=0", &fp, &cookies) && fp && !cookies,
          "fp on, cookies off");
    Check(ParseProtectionSwitch("fp=0,cookies=1", &fp, &cookies) && !fp && cookies,
          "fp off, cookies on");
    Check(ParseProtectionSwitch("fp=0,cookies=0", &fp, &cookies) && !fp && !cookies,
          "both off");
  }

  if (Failures) return 1;
  std::cout << "PROTECTION_FLAGS_TEST_PASS" << std::endl;
  return 0;
}
