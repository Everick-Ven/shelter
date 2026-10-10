// SHELTER — мастер-ключ для шифрования секретов UI (пароли, ключи ИИ).
// Windows: DPAPI (привязан к учётной записи пользователя).
// macOS: login Keychain, запись SHELTER/master-key (без запросов доступа);
//        файл secret.key 0600 — только fallback и миграция со старых версий.
// Linux: файл secret.key с правами 0600 в каталоге данных пользователя.
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <string>

#include "src/platform.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace shelter {
namespace platform {

namespace {

constexpr size_t kKeyLen = 32;

std::string ToHex(const std::string& b) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (unsigned char c : b) {
    o.push_back(d[c >> 4]);
    o.push_back(d[c & 15]);
  }
  return o;
}

bool ReadAll(const std::filesystem::path& p, std::string* out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

// Запись «всё или ничего»: сначала во временный файл рядом, затем rename поверх
// целевого. Обрыв посреди записи (сбой питания, убитый процесс) не оставляет
// усечённый secret.key — иначе следующий запуск счёл бы его нечитаемым и
// создал новый ключ, а всё зашифрованное прежним стало бы недоступным.
bool WriteAll(const std::filesystem::path& p, const std::string& data) {
  std::error_code ec;
  std::filesystem::path tmp = p;
  tmp += ".tmp";
#if defined(_WIN32)
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    f.flush();
    if (!f) {
      f.close();
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }
#else
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  ::fchmod(fd, 0600);
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n <= 0) break;
    off += (size_t)n;
  }
  const bool synced = off == data.size() && ::fsync(fd) == 0;
  const bool closed = ::close(fd) == 0;
  if (!synced || !closed) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
#endif
  std::filesystem::rename(tmp, p, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

// Файл ключа есть, но ключ из него получить не удалось (другая учётная запись
// Windows, повреждение). Затирать его новым ключом нельзя: если доступ к старому
// вернётся, зашифрованные им данные ещё можно будет расшифровать. Откладываем
// файл в сторону и только после этого создаём новый ключ.
void PreserveUnreadable(const std::filesystem::path& p) {
  std::error_code ec;
  if (!std::filesystem::exists(p, ec) || ec) return;
  for (int i = 0; i < 100; ++i) {
    std::filesystem::path dst = p;
    dst += i == 0 ? std::string(".unreadable")
                  : ".unreadable" + std::to_string(i);
    ec.clear();
    if (std::filesystem::exists(dst, ec) || ec) continue;
    std::filesystem::rename(p, dst, ec);
    return;
  }
}

std::mutex g_key_mutex;
std::atomic<bool> g_key_persistent{true};

#if defined(_WIN32)
bool Protect(const std::string& in, std::string* out) {
  DATA_BLOB i{(DWORD)in.size(), (BYTE*)in.data()}, o{};
  if (!CryptProtectData(&i, L"SHELTER", nullptr, nullptr, nullptr, 0, &o)) return false;
  out->assign((const char*)o.pbData, o.cbData);
  LocalFree(o.pbData);
  return true;
}
bool Unprotect(const std::string& in, std::string* out) {
  DATA_BLOB i{(DWORD)in.size(), (BYTE*)in.data()}, o{};
  if (!CryptUnprotectData(&i, nullptr, nullptr, nullptr, nullptr, 0, &o)) return false;
  out->assign((const char*)o.pbData, o.cbData);
  LocalFree(o.pbData);
  return true;
}
#else
bool Protect(const std::string& in, std::string* out) { *out = in; return true; }
bool Unprotect(const std::string& in, std::string* out) { *out = in; return true; }
#endif

}  // namespace

bool SecretKeyIsPersistent() {
  SecretKeyHex();
  return g_key_persistent.load();
}

std::string SecretKeyHex() {
  // Вызывается из потоков отдачи ресурсов UI: без блокировки два одновременных
  // первых вызова могли создать два разных ключа.
  std::lock_guard<std::mutex> lock(g_key_mutex);
  static std::string cached;
  static bool done = false;
  if (done) return cached;
  done = true;

  std::error_code ec;
  std::filesystem::path dir = std::filesystem::path(UserDataDir());
  std::filesystem::create_directories(dir, ec);
  const std::filesystem::path path = dir / "secret.key";

  std::string key, raw;

#if defined(__APPLE__)
  // macOS: мастер-ключ живёт в login Keychain (запись «любое приложение, без
  // запросов» — тот же модельный уровень, что DPAPI на Windows: процессы
  // юзера читают молча, на диске зашифровано паролем логина). Файл secret.key
  // — только fallback и путь миграции со старых версий.
  const char* kKcService = "SHELTER";
  const char* kKcAccount = "master-key";
  auto valid_hex = [](const std::string& s) {
    if (s.size() != kKeyLen * 2) return false;
    for (char c : s) {
      const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                      (c >= 'A' && c <= 'F');
      if (!ok) return false;
    }
    return true;
  };
  // Ключ уже в Keychain? Лимит времени обязателен: чтение чужой записи (ad-hoc
  // подпись меняет cdhash) может превратиться в модальный диалог ОС и заблокировать
  // отдачу host-bridge.js (script src) — ровно это зависание поймал CI-смоук.
  auto kcOut = std::make_shared<std::string>();
  const bool kcFound = RunTimed([kcOut, kKcService, kKcAccount]() {
    std::string v;
    if (!KeychainGet(kKcService, kKcAccount, &v)) return false;
    *kcOut = v;
    return true;
  }, 2000);
  if (kcFound && valid_hex(*kcOut)) {
    // Ключ подтверждён — legacy-файл больше не нужен (best effort).
    std::filesystem::remove(path, ec);
    cached = *kcOut;
    return cached;
  }
  // Файл со старой версии: читаем и мигрируем в Keychain.
  if (ReadAll(path, &raw) && !raw.empty() && Unprotect(raw, &key) &&
      key.size() == kKeyLen) {
    const std::string hex = ToHex(key);
    const bool migrated = RunTimed([hex, kKcService, kKcAccount]() {
      if (!KeychainPutOpen(kKcService, kKcAccount, hex)) return false;
      std::string v;
      return KeychainGet(kKcService, kKcAccount, &v) && v == hex;
    }, 2500);
    if (migrated) std::filesystem::remove(path, ec);  // миграция подтверждена
    cached = hex;
    return cached;
  }
  // Первый запуск: генерация — сначала Keychain, при неудаче файл (как раньше).
  std::random_device rd;
  key.clear();
  for (size_t i = 0; i < kKeyLen; ++i) key.push_back((char)(rd() & 0xff));
  const std::string hex = ToHex(key);
  const bool stored = RunTimed([hex, kKcService, kKcAccount]() {
    if (!KeychainPutOpen(kKcService, kKcAccount, hex)) return false;
    std::string v;
    return KeychainGet(kKcService, kKcAccount, &v) && v == hex;
  }, 2500);
  if (stored) {
    cached = hex;
    return cached;
  }
  std::string blob;
  PreserveUnreadable(path);
  if (!Protect(key, &blob) || !WriteAll(path, blob)) {
    g_key_persistent = false;
    std::fprintf(stderr, "SHELTER: master key could not be saved; secrets will not survive restart\n");
  }
  cached = hex;
  return cached;
#else
  if (ReadAll(path, &raw) && !raw.empty() && Unprotect(raw, &key) && key.size() == kKeyLen) {
    cached = ToHex(key);
    return cached;
  }
  std::random_device rd;
  key.clear();
  for (size_t i = 0; i < kKeyLen; ++i) key.push_back((char)(rd() & 0xff));
  std::string blob;
  PreserveUnreadable(path);
  if (!Protect(key, &blob) || !WriteAll(path, blob)) {
    g_key_persistent = false;
    std::fprintf(stderr, "SHELTER: master key could not be saved; secrets will not survive restart\n");
  }
  cached = ToHex(key);
  return cached;
#endif
}

}  // namespace platform
}  // namespace shelter
