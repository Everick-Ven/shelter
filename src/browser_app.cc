#include "src/browser_app.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"
#include "src/platform.h"
#include "src/protection_flags.h"
#include "src/security_paths.h"
#include "src/shell.h"

namespace shelter {

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  if (!process_type.empty()) return;
#if defined(OS_MAC)
  // Куки: если удалось подготовить запись Keychain с открытой ACL — Chromium
  // зашифрует их настоящим случайным ключом без единого запроса. Любая ошибка
  // или превышение лимита времени → use-mock-keychain (статус-кво: без
  // шифрования, но гарантированно без запросов — важно и для CI, и для первого
  // запуска). Лимит времени обязателен: обращение к Security.framework при
  // ad-hoc подписи (cdhash меняется каждую сборку) может превратиться в
  // модальный диалог ОС.
  if (!platform::RunTimed([] { return platform::EnsureCookieKeychain(); }, 3000))
    command_line->AppendSwitch("use-mock-keychain");
#endif
  // Приватный браузер: без фоновых служб Google.
  command_line->AppendSwitch("disable-background-networking");
  command_line->AppendSwitch("disable-sync");
  command_line->AppendSwitch("disable-domain-reliability");
  command_line->AppendSwitch("no-default-browser-check");
  // Ничего из введённого в формы не уходит в сеть: автофилл-сервис Google
  // (метаданные полей), проверка утечек паролей (хеши учётных данных),
  // подсказки популярных адресов, медиа-роутер и новостные фиды выключены.
  //
  // SpareRendererForSitePerProcess: «запасной» процесс рендерера создаётся
  // заранее и переиспользуется для чужих сайтов. Состояние защит приходит в
  // процесс при рождении и обновляется сообщением при навигации, а запасной
  // процесс может родиться с одним состоянием тумблеров, а страницу получить
  // уже после их переключения — то есть применить устаревшее. Отключаем: тогда
  // процесс под страницу создаётся в момент навигации, со свежим состоянием.
  command_line->AppendSwitchWithValue(
      "disable-features",
      "Translate,AutofillServerCommunication,PasswordLeakDetection,"
      "OptimizationHints,MediaRouter,InterestFeedContentSuggestions,"
      "SpareRendererForSitePerProcess");
}

void BrowserApp::OnBeforeChildProcessLaunch(
    CefRefPtr<CefCommandLine> command_line) {
  if (!command_line) return;
  // Защиты рендерера фиксируются в момент рождения процесса. Иначе новый
  // процесс (например, при переходе на другой сайт) стартовал бы со значениями
  // по умолчанию, и тумблеры фактически не работали бы на новых страницах.
  command_line->AppendSwitchWithValue(
      kProtectionSwitch, ProtectionSwitchValue(Shell::Get().ProtectionFlags()));
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().Start();
}

void FillSettings(CefSettings& settings) {
  const std::string root = platform::UserDataDir();
  const std::filesystem::path root_path = std::filesystem::u8path(root);
  const bool root_ok = security::EnsureDirectoryWithoutLink(root_path);
  if (root_ok) CefString(&settings.root_cache_path) = root;
  // Chromium-журнал по умолчанию ВЫКЛ: в предупреждениях CEF встречаются
  // адреса и имена файлов, а профиль не должен копить лишние следы сессии.
  // Диагностика включается явно: SHELTER_DIAG=1 (так делают CI-прогоны).
  const char* diag = std::getenv("SHELTER_DIAG");
  const bool diag_on = diag && *diag && std::string(diag) != "0";
  if (diag_on && root_ok) CefString(&settings.log_file) = root + "/debug.log";
  settings.log_severity = diag_on ? LOGSEVERITY_WARNING : LOGSEVERITY_DISABLE;
  CefString(&settings.locale) = "ru";
  CefString(&settings.accept_language_list) = "ru-RU,ru,en-US,en";
#if !defined(CEF_USE_SANDBOX)
  // CEF requires this when the sandbox/bootstrap support is not built.
  settings.no_sandbox = true;
#endif
}

}  // namespace shelter
