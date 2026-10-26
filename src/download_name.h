// SHELTER — очистка имени файла загрузки, предложенного сайтом.
//
// Имя приходит из Content-Disposition/URL и склеивается с папкой «Загрузки».
// Путевые разделители режутся вызовом (.filename()), но остаются зарезервированные
// имена Windows (NUL, COM1, ...), двоеточие (ADS «file.exe:stream»), завершающие
// точки/пробелы и управляющие символы — Chromium ниже частично дочищает, но
// контракт держим свой: tests/download_name_test.cc.
// Заголовок без зависимостей (без CEF).
#ifndef SHELTER_DOWNLOAD_NAME_H_
#define SHELTER_DOWNLOAD_NAME_H_

#include <cctype>
#include <string>

namespace shelter {

namespace download_detail {

inline bool IsReservedDeviceStem(const std::string& stem) {
  std::string lower;
  lower.reserve(stem.size());
  for (char c : stem) {
    lower.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "con" || lower == "prn" || lower == "aux" ||
      lower == "nul") {
    return true;
  }
  // COM1..COM9, LPT1..LPT9 (расширение Windows игнорирует).
  if (lower.size() == 4 &&
      (lower.compare(0, 3, "com") == 0 || lower.compare(0, 3, "lpt") == 0) &&
      lower[3] >= '1' && lower[3] <= '9') {
    return true;
  }
  return false;
}

}  // namespace download_detail

// Максимальная длина имени в байтах (с запасом под MAX_PATH при склейке).
inline constexpr size_t kMaxDownloadLeafBytes = 180;

inline std::string SanitizeDownloadLeaf(const std::string& name_in) {
  // Пояс поверх .filename() у вызывающей стороны: берём после последнего
  // разделителя любого вида.
  size_t cut = name_in.find_last_of("/\\");
  std::string name =
      cut == std::string::npos ? name_in : name_in.substr(cut + 1);
  for (char& c : name) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte == 0x7f || c == '<' || c == '>' || c == ':' ||
        c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' ||
        c == '*') {
      c = '_';
    }
  }
  // Windows молча режет завершающие точки и пробелы («file. » == «file») —
  // режем сами, чтобы имя на диске совпадало с показанным.
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
    name.pop_back();
  }
  size_t lead = 0;
  while (lead < name.size() && name[lead] == ' ') ++lead;
  if (lead) name.erase(0, lead);
  // Зарезервированные имена устройств: NUL, con.txt, COM1, ... — иначе запись
  // уйдёт в устройство (или будет отвергнута) вместо обычного файла.
  if (!name.empty()) {
    const size_t dot = name.find('.');
    const std::string stem =
        dot == std::string::npos ? name : name.substr(0, dot);
    if (!stem.empty() && download_detail::IsReservedDeviceStem(stem)) {
      name.insert(0, "_");
    }
  }
  // Усечение по границе UTF-8 (не рвём многобайтовый символ).
  if (name.size() > kMaxDownloadLeafBytes) {
    name.resize(kMaxDownloadLeafBytes);
    size_t start = name.size();
    while (start > 0 &&
           (static_cast<unsigned char>(name[start - 1]) & 0xC0) == 0x80) {
      --start;
    }
    if (start == 0) {
      name.clear();
    } else {
      const unsigned char lead_byte =
          static_cast<unsigned char>(name[start - 1]);
      size_t want = 1;
      if ((lead_byte & 0xE0) == 0xC0) {
        want = 2;
      } else if ((lead_byte & 0xF0) == 0xE0) {
        want = 3;
      } else if ((lead_byte & 0xF8) == 0xF0) {
        want = 4;
      }
      const size_t have = name.size() - (start - 1);
      if (have < want) {
        name.resize(start - 1);  // символ оборван границей — отрезаем
      } else if (have > want) {
        // Битый хвост (осиротевшие continuation-байты) — режем по длине.
        name.resize(start - 1 + want);
      }
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
      name.pop_back();
    }
  }
  if (name.empty()) return "download";
  return name;
}

}  // namespace shelter

#endif  // SHELTER_DOWNLOAD_NAME_H_
