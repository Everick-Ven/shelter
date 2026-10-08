// Кодирование тумблеров защит для командной строки дочерних процессов.
//
// Рендерер рождается с уже известными настройками защит: браузерная часть
// добавляет switch перед запуском процесса (BrowserApp::OnBeforeChildProcessLaunch),
// а рендерер читает его на старте (AppBase::OnBeforeCommandLineProcessing).
// Формат живёт в одном месте, чтобы «писатель» и «читатель» не разъехались:
// иначе тумблеры молча перестанут действовать на новых страницах.
#ifndef SHELTER_SRC_PROTECTION_FLAGS_H_
#define SHELTER_SRC_PROTECTION_FLAGS_H_

#include <cstddef>
#include <string>

namespace shelter {

// Имя switch в командной строке дочернего процесса.
inline const char* kProtectionSwitch = "shelter-protections";

// flags: бит 1 — анти-отпечаток, бит 2 — автосогласие cookies.
inline std::string ProtectionSwitchValue(int flags) {
  return std::string("fp=") + ((flags & 1) ? "1" : "0") +
         ",cookies=" + ((flags & 2) ? "1" : "0");
}

// Разбирает значение switch. Возвращает false, если строка не нашего формата —
// тогда выходные параметры остаются нетронутыми (значения по умолчанию).
inline bool ParseProtectionSwitch(const std::string& value, bool* fp,
                                  bool* cookies) {
  const std::size_t fp_at = value.find("fp=");
  const std::size_t cookies_at = value.find("cookies=");
  if (fp_at == std::string::npos || cookies_at == std::string::npos) return false;
  if (fp && fp_at + 3 < value.size()) *fp = value[fp_at + 3] == '1';
  if (cookies && cookies_at + 8 < value.size())
    *cookies = value[cookies_at + 8] == '1';
  return true;
}

}  // namespace shelter

#endif  // SHELTER_SRC_PROTECTION_FLAGS_H_
