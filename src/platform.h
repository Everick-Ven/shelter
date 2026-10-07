// SHELTER — платформенные хелперы (реализация: platform_win.cc / platform_mac.mm / platform_linux.cc).
#ifndef SHELTER_PLATFORM_H_
#define SHELTER_PLATFORM_H_

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace shelter {
namespace platform {

// Каталог с ресурсами UI (index.html, host-bridge.js, иконки).
std::string UiResourceDir();

// Каталог списков блокировщика (tracker-domains.txt, url-patterns.txt,
// cosmetic.css) — рядом с ресурсами UI, обновляется без пересборки.
std::string FiltersDir();

// Каталог пользовательских данных (профили, кэш).
std::string UserDataDir();

// Удалить только выделенный каталог данных SHELTER и его app-specific master key.
bool DeleteUserData();
void ShowUserDataDeletionFailure();

// Каталог «Загрузки».
std::string DownloadsDir();

// Показать файл в Проводнике / Finder.
void ShowInFolder(const std::string& path);

// Открыть ссылку во внешнем приложении (mailto:, tel:, ...).
void OpenExternal(const std::string& url);

// Буфер обмена (текст, UTF-8).
std::string ClipboardRead();
bool ClipboardWrite(const std::string& text);

// Светлая/тёмная схема нативных элементов (macOS: NSAppearance).
void SetColorScheme(bool dark);

// Скругление углов и «дыры» (прозрачные участки) нативного вида вкладки.
// handle — CefBrowserHost::GetWindowHandle(); radii = {tl, tr, br, bl} и
// holes {x, y, w, h, r} задаются в DIP относительно вида вкладки (размер
// view_w x view_h DIP). r — радиус скругления самой дырки, чтобы попап в ней
// выглядел закруглённым на фоне страницы, а не квадратным.
void ApplyViewClip(void* handle, const double radii[4],
                   const std::vector<std::array<int, 5>>& holes, int view_w,
                   int view_h);

// Диагностика (CI): уровень предка окна, которому задаётся область (Windows), и дамп цепочки HWND.
void SetClipLevel(int level);
std::string DumpWindowChain(void* handle);

// macOS: первый клик по неактивному окну приложения не должен «съедаться».
void InstallInputFixes();

// Миниатюра главного окна: CEF Views сам не реагирует на сворачивание
// (NSWindowDidMiniaturize/Deminiaturize) — без Hide()/Show() рендереры и GPU
// продолжают производить кадры для свёрнутого окна (фоновая загрузка CPU).
// nswindow = CefWindow::GetWindowHandle(); колбэки вызываются на UI-потоке.
// На Windows сворачивание нативно пробрасывается в views — там заглушки.
void WatchMainWindow(void* nswindow, void (*on_mini)(void*),
                     void (*on_demini)(void*), void* ctx);
void UnwatchMainWindow(void* nswindow);

// Мастер-ключ секретов UI: 32 байта в hex (создаётся при первом запуске).
std::string SecretKeyHex();

// macOS Keychain: чтение/запись generic-пароли БЕЗ запросов доступа
// (ACL «любое приложение» — тот же модельный уровень, что DPAPI на Windows:
//  процессы текущего пользователя читают молча, на диске зашифровано ключом
//  логина). Любая ошибка → false, у вызывающей стороны обязан быть
//  файловый fallback (secret.key, 0600).
bool KeychainGet(const char* service, const char* account, std::string* out);
bool KeychainPutOpen(const char* service, const char* account,
                     const std::string& value);

// macOS: подготовить запись Keychain для куки Chromium («Chromium Safe Storage»)
// c открытой ACL — тогда Chromium шифрует куки настоящим случайным ключом и не
// показывает запросов. false → вызывающая сторона оставляет use-mock-keychain
// (статус-кво: без шифрования, но и без запросов).
bool EnsureCookieKeychain();

// Выполнить операцию с жёстким лимитом времени. true — операция вернула true
// раньше timeout_ms; false — вернула false либо не уложилась (поток дорабатывает
// в фоне, результат игнорируется). Обязательно для любых обращений к
// Security.framework: ad-hoc подпись меняет cdhash каждую сборку, и чтение
// чужой записи может превратиться в модальный диалог ОС, который в headless-CI
// блокирует старт и отдачу ресурсов (script src) навсегда.
bool RunTimed(const std::function<bool()>& op, int timeout_ms);

// Диагностика (macOS): результат hitTest окон приложения в точке (x, y) окна, DIP от верхнего левого угла.
std::string DebugHitTest(double x, double y);

}  // namespace platform
}  // namespace shelter

#endif  // SHELTER_PLATFORM_H_
