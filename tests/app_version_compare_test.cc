// Контракт src/app_version_compare.h: проверка обновлений обязана понимать
// теги релизов («v1.0.305») и сравнивать их с kAppVersion численно —
// лексикографическое сравнение назвало бы «1.10.0» старее «1.9.9».
#include <iostream>
#include <string>

#include "src/app_version_compare.h"

namespace {

int Failures = 0;

void CheckCompare(const std::string& a, const std::string& b, int expected,
                  bool expected_ok = true) {
  bool ok = false;
  const int got = shelter::CompareAppVersions(a, b, &ok);
  if (got != expected || ok != expected_ok) {
    std::cerr << "FAIL: CompareAppVersions(\"" << a << "\", \"" << b
              << "\") = " << got << " (ok=" << ok << "), ожидалось "
              << expected << " (ok=" << expected_ok << ")" << std::endl;
    ++Failures;
  }
}

}  // namespace

int main() {
  CheckCompare("1.0.304", "1.0.304", 0);
  CheckCompare("v1.0.305", "1.0.304", 1);
  CheckCompare("1.0.303", "1.0.304", -1);
  CheckCompare("1.2", "1.2.0", 0);
  CheckCompare("1.10.0", "1.9.9", 1);
  CheckCompare("2.0", "1.99.99", 1);
  CheckCompare("  v2.0", "1.9.9", 1);
  CheckCompare("1.0.3-beta", "1.0.3", 0);
  CheckCompare("1.0.304.5", "1.0.304", 1);
  CheckCompare("1.0.304", "1.0.304.1", -1);
  CheckCompare("abc", "1.0", 0, false);
  CheckCompare("1.0", "", 0, false);
  CheckCompare("", "", 0, false);
  CheckCompare("v", "1.0", 0, false);

  if (Failures == 0) std::cout << "app-version-compare-test: OK" << std::endl;
  return Failures ? 1 : 0;
}
