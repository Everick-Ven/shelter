// Проверка хоста, для которого UI просит догрузить иконку сайта.
//
// Иконки приносит оболочка: у вкладок — при навигации (CEF сам сообщает URL
// иконки), а для хостов, которых в кэше ещё нет (например, закладки по
// умолчанию, никогда не открывавшиеся в этой сессии), UI просит догрузить
// стандартную /favicon.ico. Хост приходит из UI-страницы, поэтому перед
// подстановкой в URL он проверяется здесь: разрешены только доменные имена
// (буквы, цифры, дефис, точка), без схемы, порта, пути и пробелов.
// Заголовок без зависимостей: тестируется обычным c++ (см.
// tests/favicon_hosts_test.cc) и подключается в клиентах оболочки.
#ifndef SHELTER_FAVICON_HOSTS_H_
#define SHELTER_FAVICON_HOSTS_H_

#include <string>

namespace shelter {

// Максимальная длина доменного имени (RFC 1035 + запас на поддомены).
constexpr size_t kMaxFaviconHostLength = 253;

inline bool IsValidFaviconHost(const std::string& host) {
  if (host.size() < 4 || host.size() > kMaxFaviconHostLength) return false;
  if (host.front() == '.' || host.back() == '.' || host.front() == '-') return false;
  size_t dots = 0, label = 0;
  for (const char c : host) {
    if (c == '.') {
      if (label == 0 || label > 63) return false;  // пустая или слишком длинная метка
      ++dots;
      label = 0;
      continue;
    }
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    const bool digit = c >= '0' && c <= '9';
    if (!letter && !digit && c != '-') return false;  // только буквы, цифры и дефис
    ++label;
  }
  if (label == 0 || label > 63) return false;
  return dots >= 1;  // домен первого уровня обязателен
}

// URL иконки по умолчанию: только https. Файл /favicon.ico спрашивается тем же
// сетевым контекстом, что и вкладка; для хостов с иконкой в другом месте
// никакой догрузки не нужно — её уже принесла навигация.
inline std::string FaviconUrlForHost(const std::string& host) {
  return "https://" + host + "/favicon.ico";
}

}  // namespace shelter

#endif  // SHELTER_FAVICON_HOSTS_H_
