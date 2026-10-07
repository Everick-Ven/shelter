// Блокировщик трекеров и рекламы SHELTER.
// Принцип работы как у классических контент-блокеров (uBlock Origin, AdGuard):
//  1) сетевая фильтрация — запросы к доменам из списка и к рекламным
//     URL-шаблонам отменяются в OnBeforeResourceLoad (RV_CANCEL);
//  2) косметическая фильтрация — в страницы внедряется CSS, скрывающий
//     типовые рекламные контейнеры;
//  3) учёт — каждый заблокированный запрос попадает в статистику UI.
// Списки правил лежат рядом с интерфейсом: <resources>/filters/*.txt и
// cosmetic.css — их можно обновлять без пересборки.

#ifndef SHELTER_SRC_BLOCKER_H_
#define SHELTER_SRC_BLOCKER_H_

#include <string>

namespace shelter {
namespace blocker {

// Читает правила с диска (безопасно вызывать повторно — перезагружает).
void LoadRules();

// Включено ли блокирование (приходит из UI-настройки «Блокировка трекеров»).
void SetEnabled(bool on);
bool Enabled();

// true — запрос надо отменить. kind: 't' (трекер по домену) / 'a' (реклама по
// шаблону URL). page_url — адрес страницы-владельца для third-party проверки.
bool ShouldBlock(const std::string& url, const std::string& page_url,
                 char* kind);

// Сообщает UI о блокировке (событие «blocked», счётчики в дашборде).
// Вызывается с IO-потока — сам переносит работу в UI-поток.
void RecordBlock(const std::string& host, char kind);

// CSS косметической фильтрации (пусто, если файла нет).
const std::string& CosmeticCss();

}  // namespace blocker
}  // namespace shelter

#endif  // SHELTER_SRC_BLOCKER_H_
