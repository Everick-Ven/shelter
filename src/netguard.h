// HTTPS-only + DoH (шифрованный DNS) — SHELTER.
//
// Принципы — те же, что в больших браузерах по состоянию на октябрь 2026:
//  * HTTPS-only: Chrome 154 «Always Use Secure Connections» включает режим по
//    умолчанию — навигации автоматически поднимаются с http:// на https://,
//    при недоступности HTTPS показывается интерстишиал с осознанным переходом
//    на http://; loopback/частные адреса и имена без точек не трогаются.
//  * DoH: Chromium решает сам, куда слать DNS-запросы, если в профильных
//    настройках заданы «dns_over_https.mode=secure» и шаблон провайдера
//    («dns_over_https.templates»). Записываем их через
//    CefRequestContext::SetPreference (базовый класс CefPreferenceManager) —
//    это настоящий PrefService профиля, DnsConfigService подхватывает
//    изменение на лету, без перезапуска.
//
// Управление приходит из UI: тумблер «Безопасный HTTPS + приватный DNS»
// (https.setEnabled) и выбор провайдера (dns.setProvider).

#ifndef SHELTER_SRC_NETGUARD_H_
#define SHELTER_SRC_NETGUARD_H_

#include <optional>
#include <string>

#include "include/cef_request_context.h"

namespace shelter {
namespace netguard {

// Включён ли HTTPS-only (автоподъём навигаций на https://).
void SetHttpsOnlyEnabled(bool on);
bool HttpsOnlyEnabled();

// Провайдер DoH: "cf" | "google" | "quad9" | "sys" (системный, без DoH).
void SetDohProvider(const std::string& provider);
std::string DohProvider();

// Шаблон DoH-эндпоинта для провайдера (пусто для "sys"/неизвестного).
std::string DohTemplates(const std::string& provider);

// Записывает настройки DoH в профиль контекста. Только UI-поток и только
// после инициализации контекста (иначе профиль ещё не существует).
void ApplyDohPrefs(CefRefPtr<CefRequestContext> context);

// https://-версия url, если навигацию надо поднять; иначе nullopt.
// Не трогает: не-http схемы, адреса с явным портом, loopback/частные
// диапазоны, имена без точек (интранет) и хосты из исключений пользователя.
std::optional<std::string> TryUpgrade(const std::string& url);

// Пары «поднятый адрес → исходный http://»: по ним интерстишиал знает,
// куда предлагать перейти по нажатию «Продолжить по HTTP».
void RememberUpgrade(const std::string& https_url, const std::string& http_url);
// Возвращает исходный http://-адрес для неудавшегося апгрейда и сразу
// регистрирует для его хоста сессионное исключение (как у Chrome: после
// показа интерстишиала сайт больше не дёргается повторными апгрейдами).
std::optional<std::string> TakeUpgradeFallback(const std::string& https_url);

// Сессионное исключение: разрешить этому хосту незашифрованный HTTP.
void AllowInsecure(const std::string& host);

// Честная статистика: событие «blocked» {h:1} для дашборда.
// Вызывается с IO-потока — сам переносит работу в UI-поток.
void RecordUpgrade(const std::string& host);

}  // namespace netguard
}  // namespace shelter

#endif  // SHELTER_SRC_NETGUARD_H_
