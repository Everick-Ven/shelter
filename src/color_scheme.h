// Цветовая схема приложения: UI-тема -> режим Chromium.
//
// Тема интерфейса — это не только CSS: браузер обязан сообщать веб-контенту
// свою цветовую схему (от неё зависят prefers-color-scheme на страницах,
// скроллбары и системные элементы управления Chromium). CEF для этого даёт
// CefRequestContext::SetChromeColorScheme. Здесь — чистая функция перевода
// режима из интерфейса в «темно/светло», чтобы её можно было проверить без CEF
// (tests/color_scheme_test.cc) и чтобы тёмная схема не терялась на путях,
// отличных от явного «light».
#ifndef SHELTER_COLOR_SCHEME_H_
#define SHELTER_COLOR_SCHEME_H_

#include <string>

namespace shelter {

// Режим приходит из UI: 'light' — светлая, всё остальное (включая 'dark' и
// неизвестные значения) — тёмная, потому что тёмная тема в SHELTER основная:
// ошибка разбора не должна осветлять интерфейс контента.
inline bool ColorSchemeDarkFromMode(const std::string& mode) {
  return mode != "light";
}

}  // namespace shelter

#endif  // SHELTER_COLOR_SCHEME_H_
