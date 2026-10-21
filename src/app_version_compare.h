// SHELTER — сравнение версий вида major.minor.patch[.build].
//
// Проверка обновлений сверяет tag_name официального релиза («v1.0.304») с
// текущей kAppVersion («1.0.303»). Сравнение числовое покомпонентно, а не
// лексикографическое («1.10.0» новее «1.9.9»). Заголовок без зависимостей
// (без CEF): контракт — tests/app_version_compare_test.cc.
#ifndef SHELTER_APP_VERSION_COMPARE_H_
#define SHELTER_APP_VERSION_COMPARE_H_

#include <string>
#include <vector>

namespace shelter {

// -1 (a < b), 0 (равны), 1 (a > b). Ведущие пробелы и 'v'/'V' игнорируются,
// недостающие компоненты считаются нулями («1.2» == «1.2.0»), хвост после
// чисел обрезается («1.0.3-beta» — числовая часть 1.0.3). ok=false, если хотя
// бы одна сторона не начинается с цифры (сравнивать бессмысленно).
inline int CompareAppVersions(const std::string& a, const std::string& b,
                              bool* ok) {
  auto parse = [](const std::string& s, std::vector<unsigned long>* out) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
    while (i < s.size()) {
      unsigned long v = 0;
      bool digits = false;
      while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        digits = true;
        v = v * 10 + static_cast<unsigned long>(s[i] - '0');
        if (v > 1000000000ul) v = 1000000000ul;  // насыщение, без переполнения
        ++i;
      }
      if (!digits) break;  // суффикс («-beta», «+build») — стоп
      out->push_back(v);
      if (i < s.size() && s[i] == '.') {
        ++i;
        if (i >= s.size() || s[i] < '0' || s[i] > '9') break;  // «1.» — стоп
        continue;
      }
      break;
    }
    return !out->empty();
  };
  std::vector<unsigned long> va, vb;
  const bool oka = parse(a, &va);
  const bool okb = parse(b, &vb);
  if (ok) *ok = oka && okb;
  if (!oka || !okb) return 0;
  const size_t n = va.size() > vb.size() ? va.size() : vb.size();
  for (size_t i = 0; i < n; ++i) {
    const unsigned long x = i < va.size() ? va[i] : 0;
    const unsigned long y = i < vb.size() ? vb[i] : 0;
    if (x < y) return -1;
    if (x > y) return 1;
  }
  return 0;
}

}  // namespace shelter

#endif  // SHELTER_APP_VERSION_COMPARE_H_
