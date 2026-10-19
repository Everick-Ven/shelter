// Контракт встроенного Public Suffix List (src/public_suffix.h).
//
// Строгий режим и allow-разрешения опираются на RegistrableDomainPsl:
// ошибка здесь — либо ложные блокировки своих скриптов, либо пропуск чужих
// (shared-хостинги: github.io, appspot.com, ...). Тест держит канонические
// случаи алгоритма PSL: точные правила, групповые (*.), исключения (!),
// правило по умолчанию «*», а также отклонение SHELTER (сам суффикс
// возвращается как есть, а не пустой строкой).
#include <iostream>
#include <string>

#include "src/public_suffix.h"

namespace {

int Failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << std::endl;
  ++Failures;
}

void CheckDomain(const std::string& host, const std::string& expected) {
  const std::string got = shelter::RegistrableDomainPsl(host);
  if (got != expected) {
    std::cerr << "FAIL: RegistrableDomainPsl(\"" << host << "\") = \"" << got
              << "\", ожидалось \"" << expected << "\"" << std::endl;
    ++Failures;
  }
}

}  // namespace

int main() {
  // Обычные домены и поддомены.
  CheckDomain("example.com", "example.com");
  CheckDomain("www.example.com", "example.com");
  CheckDomain("a.b.example.com", "example.com");
  CheckDomain("example.ru", "example.ru");
  CheckDomain("WWW.EXAMPLE.COM", "example.com");

  // Двухуровневые суффиксы (эквивалент старого ручного списка).
  CheckDomain("example.co.uk", "example.co.uk");
  CheckDomain("a.example.co.uk", "example.co.uk");
  CheckDomain("example.com.br", "example.com.br");
  CheckDomain("sub.example.com.au", "example.com.au");
  CheckDomain("example.co.jp", "example.co.jp");

  // Shared-хостинги: каждый тенант — отдельный регистрируемый домен.
  // (На ручном списке это ломалось: всё схлопывалось в «github.io».)
  CheckDomain("user.github.io", "user.github.io");
  CheckDomain("evil.github.io", "evil.github.io");
  CheckDomain("myapp.herokuapp.com", "myapp.herokuapp.com");
  CheckDomain("project.appspot.com", "project.appspot.com");
  CheckDomain("site.netlify.app", "site.netlify.app");
  CheckDomain("app.vercel.app", "app.vercel.app");

  // Групповые правила и исключения (канонические примеры из спецификации PSL).
  // «*.kobe.jp»: каждый прямой потомок — суффикс; наивные «последние две
  // метки» дали бы здесь «kobe.jp» и «a.kobe.jp» — неверно.
  CheckDomain("a.kobe.jp", "a.kobe.jp");
  CheckDomain("b.a.kobe.jp", "b.a.kobe.jp");
  CheckDomain("city.kobe.jp", "city.kobe.jp");
  CheckDomain("city.kawasaki.jp", "city.kawasaki.jp");

  // Сам суффикс — возвращается как есть (отклонение SHELTER, не пусто).
  CheckDomain("github.io", "github.io");
  CheckDomain("co.uk", "co.uk");
  CheckDomain("com", "com");

  // Односоставные имена, IP, FQDN-точка — возвращаются как есть.
  CheckDomain("localhost", "localhost");
  CheckDomain("intranet", "intranet");
  CheckDomain("127.0.0.1", "127.0.0.1");
  CheckDomain("example.com.", "example.com");
  CheckDomain("", "");

  Check(Failures == 0, "все проверки прошли");
  if (Failures == 0) std::cout << "public-suffix-test: OK" << std::endl;
  return Failures ? 1 : 0;
}
