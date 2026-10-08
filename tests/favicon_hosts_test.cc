// Контракт догрузки иконок сайтов по запросу UI (favicon.ensure).
//
// Хост приходит из UI-страницы и подставляется в URL сетевого запроса, поэтому
// проверка имени — часть безопасности моста: пропустить схему, порт, путь,
// пробел или не-ASCII нельзя, иначе из закладки получится запрос «куда-то
// ещё». Тест держит обе стороны: правила имени и форму URL /favicon.ico.
#include <cassert>
#include <iostream>
#include <string>

#include "src/favicon_hosts.h"

namespace {

int Failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << std::endl;
  ++Failures;
}

}  // namespace

int main() {
  using shelter::FaviconUrlForHost;
  using shelter::IsValidFaviconHost;

  // Обычные доменные имена.
  Check(IsValidFaviconHost("example.com"), "example.com принят");
  Check(IsValidFaviconHost("news.example.org"), "поддомен принят");
  Check(IsValidFaviconHost("sberbank.ru"), "ru-домен принят");
  Check(IsValidFaviconHost("xn--80aesfpebagmfblc0a.xn--p1ai"), "punycode принят");
  Check(IsValidFaviconHost("a1.b2.example.co.uk"), "цифры и дефисы в метках приняты");

  // Отсекаем всё, что не доменное имя.
  Check(!IsValidFaviconHost(""), "пустая строка отброшена");
  Check(!IsValidFaviconHost("com"), "одна метка без домена отброшена");
  Check(!IsValidFaviconHost("localhost"), "localhost отброшен");
  Check(!IsValidFaviconHost(".example.com"), "ведущая точка отброшена");
  Check(!IsValidFaviconHost("example.com."), "замыкающая точка отброшена");
  Check(!IsValidFaviconHost("-example.com"), "ведущий дефис отброшен");
  Check(!IsValidFaviconHost("example..com"), "пустая метка отброшена");
  Check(!IsValidFaviconHost("https://example.com"), "схема отброшена");
  Check(!IsValidFaviconHost("example.com:443"), "порт отброшен");
  Check(!IsValidFaviconHost("example.com/favicon.ico"), "путь отброшен");
  Check(!IsValidFaviconHost("example com"), "пробел отброшен");
  Check(!IsValidFaviconHost("example.com\n"), "перевод строки отброшен");
  Check(!IsValidFaviconHost("exa_mple.com"), "подчёркивание отброшено");
  Check(!IsValidFaviconHost("пример.рф"), "не-ASCII отброшен (в URL уходит punycode)");
  Check(!IsValidFaviconHost(std::string(300, 'a') + ".com"), "слишком длинное имя отброшено");
  Check(!IsValidFaviconHost(std::string(64, 'a') + ".com"), "слишком длинная метка отброшена");
  Check(IsValidFaviconHost(std::string(63, 'a') + ".com"), "предельная метка принята");

  // URL: только https и только стандартный путь иконки.
  Check(FaviconUrlForHost("example.com") == "https://example.com/favicon.ico",
        "URL иконки по умолчанию");
  Check(FaviconUrlForHost("news.example.org") ==
            "https://news.example.org/favicon.ico",
        "URL для поддомена");

  if (Failures) {
    std::cerr << "FAILURES: " << Failures << std::endl;
    return 1;
  }
  std::cout << "FAVICON_HOSTS_TEST_PASS" << std::endl;
  return 0;
}
