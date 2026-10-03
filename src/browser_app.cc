#include "src/browser_app.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"
#include "src/platform.h"
#include "src/shell.h"

namespace shelter {

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
  if (!process_type.empty()) return;
#if defined(OS_MAC)
  // Куки: если удалось подготовить запись Keychain с открытой ACL — Chromium
  // зашифрует их настоящим случайным ключом без единого запроса. Если что-то
  // пошло не так (чужая запись, нет доступа, keychain недоступен) — оставляем
  // use-mock-keychain: как раньше, без шифрования, но гарантированно без
  // запросов доступа (важно и для CI, и для первого запуска).
  //
  // Вызов ОГРАНИЧЕН ПО ВРЕМЕНИ: любые обращения к Security.framework (в том
  // числе системные диалоги, если ACL вдруг потребует авторизацию) не должны
  // блокировать старт приложения — по таймауту оставляем статус-кво.
  {
    auto kc = std::make_shared<std::atomic<int>>(-1);  // -1 pending, 0/1
    std::thread([kc] { kc->store(platform::EnsureCookieKeychain() ? 1 : 0); })
        .detach();
    for (int i = 0; i < 30 && kc->load() < 0; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (kc->load() != 1)
      command_line->AppendSwitch("use-mock-keychain");
  }
#endif
  // Приватный браузер: без фоновых служб Google.
  command_line->AppendSwitch("disable-background-networking");
  command_line->AppendSwitch("disable-sync");
  command_line->AppendSwitch("no-default-browser-check");
  command_line->AppendSwitchWithValue("disable-features", "Translate");
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().Start();
}

void FillSettings(CefSettings& settings) {
  const std::string root = platform::UserDataDir();
  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  CefString(&settings.root_cache_path) = root;
  CefString(&settings.log_file) = root + "/debug.log";
  settings.log_severity = LOGSEVERITY_WARNING;
  CefString(&settings.locale) = "ru";
  CefString(&settings.accept_language_list) = "ru-RU,ru,en-US,en";
  settings.no_sandbox = true;
}

}  // namespace shelter
