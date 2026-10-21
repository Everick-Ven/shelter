// SHELTER — чистые сетевые помощники без зависимостей (без CEF).
//
// IsPrivateHost решает, какие хосты HTTPS-only НЕ поднимает на https://:
// loopback, link-local, приватные диапазоны, CGNAT — роутеры, принтеры и
// интранет-порталы почти никогда не имеют валидного сертификата.
// Ошибка в одну сторону — сломанный интранет, в другую — тихий пропуск
// публичного хоста без апгрейда, поэтому контракт держит
// tests/net_utils_test.cc.
#ifndef SHELTER_NET_UTILS_H_
#define SHELTER_NET_UTILS_H_

#include <cctype>
#include <cstdio>
#include <string>

namespace shelter {

inline std::string LowerHost(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

// Разбирает IPv4-адрес; возвращает false, если это не точечная четверка.
inline bool ParseIpv4Host(const std::string& host, int octets[4]) {
  int parts[4];
  char tail;
  if (std::sscanf(host.c_str(), "%d.%d.%d.%d%c", &parts[0], &parts[1],
                  &parts[2], &parts[3], &tail) != 4) {
    return false;
  }
  for (int i = 0; i < 4; i++) {
    if (parts[i] < 0 || parts[i] > 255) return false;
    octets[i] = parts[i];
  }
  return true;
}

inline bool IsPrivateV4Octets(const int octets[4]) {
  const int o0 = octets[0], o1 = octets[1];
  if (o0 == 127 || o0 == 0 || o0 == 10) return true;
  if (o0 == 169 && o1 == 254) return true;  // link-local
  if (o0 == 192 && o1 == 168) return true;
  if (o0 == 172 && o1 >= 16 && o1 <= 31) return true;
  if (o0 == 100 && o1 >= 64 && o1 <= 127) return true;  // CGNAT
  return false;
}

inline bool IsPrivateHost(const std::string& host_in) {
  std::string host = LowerHost(host_in);
  while (!host.empty() && host.back() == '.') host.pop_back();  // FQDN-точка
  if (host.empty()) return true;
  if (host.front() == '[' && host.back() == ']')
    host = host.substr(1, host.size() - 2);  // IPv6 в скобках
  if (host.empty()) return true;
  if (host == "localhost" || host == "::1" || host == "::") return true;
  if (host.size() > 10 && host.compare(host.size() - 10, 10, ".localhost") == 0)
    return true;
  // IPv6-ветка — только для литералов с ':'. Проверка префиксов fe80/fc/fd по
  // первым символам любого имени ошибочно считала приватными публичные хосты
  // вида «fc.example.com» — они тихо оставались без HTTPS-апгрейда.
  if (host.find(':') != std::string::npos) {
    // Встроенный IPv4 (напр. ::ffff:192.168.0.1): решаем по нему.
    if (host.find('.') != std::string::npos) {
      const size_t last_colon = host.rfind(':');
      int o[4];
      if (last_colon != std::string::npos &&
          ParseIpv4Host(host.substr(last_colon + 1), o)) {
        return IsPrivateV4Octets(o);
      }
    }
    // Первая hex-группа (ведущее "::" пропускаем: fe80::/10 и fc00::/7
    // начинаются с ненулевой группы, пустое начало — ::/128 или ::1/128).
    size_t start = 0;
    while (start < host.size() && host[start] == ':') ++start;
    size_t end = start;
    while (end < host.size() && end - start < 4 &&
           std::isxdigit(static_cast<unsigned char>(host[end]))) {
      ++end;
    }
    if (end == start) return true;
    unsigned value = 0;
    for (size_t i = start; i < end; ++i) {
      const char c = host[i];
      value = value * 16 +
              static_cast<unsigned>(
                  c <= '9' ? c - '0' : c - 'a' + 10);
    }
    if (value >= 0xfe80 && value <= 0xfebf) return true;  // fe80::/10
    if (value >= 0xfc00 && value <= 0xfdff) return true;  // fc00::/7
    return false;
  }
  int o[4];
  if (ParseIpv4Host(host, o)) return IsPrivateV4Octets(o);
  return false;
}

}  // namespace shelter

#endif  // SHELTER_NET_UTILS_H_
