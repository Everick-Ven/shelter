// SHELTER — единый ответ на вопрос «песочница скомпилирована?».
//
// Ловушка, в которую уже попадали: на Windows (CEF M138+) дистрибутив дефайнит
// ТОЛЬКО CEF_USE_BOOTSTRAP (песочница — подписанный bootstrap.exe, грузящий
// DLL клиента), а на macOS — CEF_USE_SANDBOX (линковка libcef_sandbox).
// Проверка `#if !defined(CEF_USE_SANDBOX)` в FillSettings молча выключала
// песочницу в рантайме на Windows при зелёном CI-гейте (гейт видел только
// build-конфигурацию). Любой макрос из пары означает: песочница есть и
// settings.no_sandbox обязана остаться false.
//
// Заголовок без зависимостей (без CEF): контракт держит
// tests/sandbox_config_test.cc, который CI собирает трижды — под каждый
// макрос и без них.
#ifndef SHELTER_SANDBOX_CONFIG_H_
#define SHELTER_SANDBOX_CONFIG_H_

namespace shelter {

inline constexpr bool SandboxCompiledIn() {
#if defined(CEF_USE_SANDBOX) || defined(CEF_USE_BOOTSTRAP)
  return true;
#else
  return false;
#endif
}

}  // namespace shelter

#endif  // SHELTER_SANDBOX_CONFIG_H_
