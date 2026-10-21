// Контракт src/net_utils.h: какие хосты HTTPS-only не поднимает на https://.
//
// Ошибка классификации бьёт в одну из двух сторон: либо ломается интранет
// (лишний апгрейд приватного адреса), либо публичный хост тихо остаётся на
// http:// (ложно-приватный). Тест фиксирует оба направления, включая регрессию
// «fc.example.com считался приватным» и покрытие fe80::/10 целиком.
#include <iostream>
#include <string>

#include "src/net_utils.h"

namespace {

int Failures = 0;

void CheckPrivate(const std::string& host, bool expected) {
  const bool got = shelter::IsPrivateHost(host);
  if (got != expected) {
    std::cerr << "FAIL: IsPrivateHost(\"" << host << "\") = "
              << (got ? "true" : "false") << ", ожидалось "
              << (expected ? "true" : "false") << std::endl;
    ++Failures;
  }
}

}  // namespace

int main() {
  // IPv4: приватные и специальные диапазоны.
  CheckPrivate("127.0.0.1", true);
  CheckPrivate("127.1.2.3", true);
  CheckPrivate("0.0.0.0", true);
  CheckPrivate("10.0.0.1", true);
  CheckPrivate("10.255.255.255", true);
  CheckPrivate("192.168.0.1", true);
  CheckPrivate("172.16.0.1", true);
  CheckPrivate("172.31.255.255", true);
  CheckPrivate("169.254.1.1", true);
  CheckPrivate("100.64.0.1", true);
  CheckPrivate("100.127.255.255", true);
  // IPv4: публичные, включая границы диапазонов.
  CheckPrivate("8.8.8.8", false);
  CheckPrivate("1.1.1.1", false);
  CheckPrivate("11.0.0.1", false);
  CheckPrivate("172.15.255.255", false);
  CheckPrivate("172.32.0.1", false);
  CheckPrivate("192.167.1.1", false);
  CheckPrivate("193.168.1.1", false);
  CheckPrivate("100.63.255.255", false);
  CheckPrivate("100.128.0.1", false);
  CheckPrivate("169.253.1.1", false);
  CheckPrivate("169.255.1.1", false);

  // Имена: localhost и производные — приватные.
  CheckPrivate("localhost", true);
  CheckPrivate("LOCALHOST", true);
  CheckPrivate("foo.localhost", true);
  CheckPrivate("localhost.", true);
  CheckPrivate("", true);
  // Имена: обычные публичные.
  CheckPrivate("example.com", false);
  CheckPrivate("EXAMPLE.COM", false);
  CheckPrivate("example.com.", false);
  CheckPrivate("foo.localhost.evil.com", false);
  // Регрессия: имена на fc/fd/fe80 — публичные, а не IPv6-литералы.
  CheckPrivate("fc.example.com", false);
  CheckPrivate("fd00.example.net", false);
  CheckPrivate("fe80.example.org", false);
  CheckPrivate("fc00.test", false);

  // IPv6: loopback/unspecified/link-local/unique-local.
  CheckPrivate("::1", true);
  CheckPrivate("[::1]", true);
  CheckPrivate("::", true);
  CheckPrivate("fe80::1", true);
  CheckPrivate("FE80::A", true);
  CheckPrivate("fe80::", true);
  CheckPrivate("fe90::1", true);
  CheckPrivate("febf:1234::1", true);
  CheckPrivate("fc00::1", true);
  CheckPrivate("fd12:3456::1", true);
  // IPv6: публичные.
  CheckPrivate("2001:db8::1", false);
  CheckPrivate("fec0::1", false);
  CheckPrivate("fe7f:ffff::1", false);
  CheckPrivate("[2001:db8::1]", false);
  // IPv4-mapped: решает встроенный адрес.
  CheckPrivate("::ffff:192.168.0.1", true);
  CheckPrivate("::ffff:10.9.8.7", true);
  CheckPrivate("::ffff:8.8.8.8", false);

  if (Failures == 0) std::cout << "net-utils-test: OK" << std::endl;
  return Failures ? 1 : 0;
}
