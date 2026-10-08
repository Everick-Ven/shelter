// Загрузка расширений в движок Chromium.
//
// CEF вырезал embedder-API расширений (CefRequestContext::LoadExtension) в
// M127, но сам движок CEF — это Chrome runtime: система расширений Chromium в
// сборке есть (CEF патчит chrome/browser/extensions/api/*, чтобы chrome.tabs
// видел лёгкие Alloy-вкладки — issue #3681), а CEF нигде их не отключает.
// Единственный оставшийся путь — штатный аргумент Chromium
// `--load-extension=<пути распакованных MV3-расширений через запятую>`,
// который читает ExtensionService при инициализации профиля.
//
// Отсюда две особенности, которые эта логика обязана учитывать:
//   * список путей фиксируется ДО старта CEF (аргумент командной строки),
//     поэтому только что установленное расширение подхватывается при
//     следующем запуске — интерфейс обязан говорить это честно;
//   * Chromium 154 не грузит manifest v2, поэтому MV2-расширения остаются на
//     ручном внедрении content-scripts оболочкой (см. InjectExtScripts).
//
// Заголовок CEF-free: проверяется обычным c++ тестом без сборки CEF.
#ifndef SHELTER_EXTENSION_ENGINE_H_
#define SHELTER_EXTENSION_ENGINE_H_

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace shelter {

// Больше 32 расширений в командную строку не отдаём: значение switch'а
// разбирается движком целиком, и очень длинная строка — лишний риск на
// Windows (ограничение CreateProcess).
inline constexpr size_t kMaxEngineExtensions = 32;

namespace ext_engine_detail {

inline std::string ReadTextFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::string();
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

inline bool WriteTextFile(const std::string& path, const std::string& text) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(f);
}

}  // namespace ext_engine_detail

// Каталоги-кандидаты, где лежат распакованные расширения оболочки.
//
// Основной — «<данные>/Default/Extensions»: кэш глобального контекста запросов
// равен пути профиля Default (CEF создаёт глобальный контекст из профиля,
// chrome_main_delegate_cef.cc CreateNewBrowserContext). Второй —
// «<данные>/Extensions» (раскладка, когда кэш равен каталогу данных). Оба
// сканируются, чтобы установленные расширения не терялись при смене раскладки;
// один и тот же id берётся из первого найденного каталога.
inline std::vector<std::string> ExtRootCandidates(
    const std::string& user_data_dir) {
  std::vector<std::string> out;
  if (user_data_dir.empty()) return out;
  out.push_back(user_data_dir + "/Default/Extensions");
  out.push_back(user_data_dir + "/Extensions");
  return out;
}

// Файл режима расширений: решение принимается до старта CEF, а настройка
// живёт в интерфейсе — поэтому режим хранится рядом с данными, а не в
// localStorage профиля.
inline std::string ExtModeFile(const std::string& user_data_dir) {
  if (user_data_dir.empty()) return std::string();
  return user_data_dir + "/ext_engine";
}

// Разбор файла режима. Неизвестное содержимое (в том числе пустой файл)
// трактуется как «движок»: это безопасный default только в том смысле, что
// MV2 всё равно остаются на ручном внедрении.
inline bool ExtEngineEnabledByText(const std::string& text) {
  std::string t;
  for (char c : text) {
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') t.push_back(c);
  }
  return t != "shell";
}

inline std::string ExtEngineModeText(bool engine) {
  return engine ? std::string("engine\n") : std::string("shell\n");
}

inline bool ExtEngineEnabled(const std::string& user_data_dir) {
  const std::string path = ExtModeFile(user_data_dir);
  if (path.empty()) return true;
  return ExtEngineEnabledByText(ext_engine_detail::ReadTextFile(path));
}

inline bool ExtEngineSetEnabled(const std::string& user_data_dir, bool on) {
  const std::string path = ExtModeFile(user_data_dir);
  if (path.empty()) return false;
  return ext_engine_detail::WriteTextFile(path, ExtEngineModeText(on));
}

// Годится ли путь для значения `--load-extension`. Значение разбирается по
// запятым, поэтому запятая в пути сломала бы соседние элементы; кавычки и
// переводы строк в аргументе командной строки опасны по той же причине.
inline bool ExtEnginePathSafe(const std::string& path) {
  if (path.empty()) return false;
  for (char c : path) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') return false;
  }
  return true;
}

// Значение `--load-extension`: абсолютные пути через запятую, не больше
// kMaxEngineExtensions штук. Опасные пути пропускаются (и попадают в skipped).
inline std::string BuildLoadExtensionValue(
    const std::vector<std::string>& dirs, size_t max_count,
    std::vector<std::string>* skipped) {
  std::string value;
  size_t used = 0;
  for (const auto& dir : dirs) {
    if (!ExtEnginePathSafe(dir)) {
      if (skipped) skipped->push_back(dir);
      continue;
    }
    if (used >= max_count) {
      if (skipped) skipped->push_back(dir);
      continue;
    }
    if (!value.empty()) value.push_back(',');
    value += dir;
    ++used;
  }
  return value;
}

inline std::string BuildLoadExtensionValue(
    const std::vector<std::string>& dirs) {
  return BuildLoadExtensionValue(dirs, kMaxEngineExtensions, nullptr);
}

// Имя каталога (id расширения) из абсолютного пути. Разделители путей у
// Windows и macOS/Linux разные, поэтому сравнение «этот каталог передан
// движку?» делается по последнему компоненту, а не по строке пути целиком.
inline std::string ExtDirName(const std::string& path) {
  size_t end = path.size();
  while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) --end;
  const size_t slash = path.find_last_of("/\\", end == 0 ? 0 : end - 1);
  if (slash == std::string::npos) return path.substr(0, end);
  return path.substr(slash + 1, end - slash - 1);
}

// manifest_version из текста манифеста. Разбор намеренно простой (движок всё
// равно проверит манифест целиком): ищем ключ "manifest_version", затем
// двоеточие и целое число. Манифест без версии считаем v1.
inline int ExtManifestVersionFromText(const std::string& manifest) {
  const std::string key = "\"manifest_version\"";
  size_t pos = manifest.find(key);
  while (pos != std::string::npos) {
    size_t i = pos + key.size();
    while (i < manifest.size() &&
           std::isspace(static_cast<unsigned char>(manifest[i]))) {
      ++i;
    }
    if (i < manifest.size() && manifest[i] == ':') {
      ++i;
      while (i < manifest.size() &&
             std::isspace(static_cast<unsigned char>(manifest[i]))) {
        ++i;
      }
      int value = 0;
      bool digits = false;
      while (i < manifest.size() &&
             std::isdigit(static_cast<unsigned char>(manifest[i]))) {
        digits = true;
        value = value * 10 + (manifest[i] - '0');
        ++i;
      }
      if (digits) return value;
    }
    pos = manifest.find(key, pos + 1);
  }
  return 1;
}

// Каталог расширения с манифестом v3 и выше — то, что движок Chromium 154
// способен загрузить (MV2 в нём отключён).
inline bool ExtDirIsEngineLoadable(const std::string& dir_path) {
  const std::string manifest =
      ext_engine_detail::ReadTextFile(dir_path + "/manifest.json");
  if (manifest.empty()) return false;
  return ExtManifestVersionFromText(manifest) >= 3;
}

// Собрать каталоги расширений для аргумента --load-extension: обход корней
// (порядок важен — первый найденный id побеждает), только безопасные имена
// каталогов, только manifest v3+, не больше max_count штук.
inline std::vector<std::string> ExtEngineCollectDirs(
    const std::vector<std::string>& roots, size_t max_count) {
  std::vector<std::string> out;
  for (const auto& root : roots) {
    if (out.size() >= max_count) break;
    std::error_code ec;
    std::filesystem::directory_iterator it(root, ec);
    if (ec) continue;
    const std::filesystem::directory_iterator end;
    for (; it != end && out.size() < max_count; it.increment(ec)) {
      if (ec) break;
      const std::filesystem::path dir = it->path();
      const std::string id = dir.filename().string();
      if (id.empty() || id.rfind("_tmp", 0) == 0) continue;
      bool safe = true;
      for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                        c == '-';
        if (!ok) {
          safe = false;
          break;
        }
      }
      if (!safe) continue;
      std::error_code dec;
      if (!std::filesystem::is_directory(dir, dec) || dec) continue;
      const std::string path = dir.string();
      if (!ExtEnginePathSafe(path) || !ExtDirIsEngineLoadable(path)) continue;
      bool dup = false;
      for (const auto& seen : out) {
        if (ExtDirName(seen) == id) {
          dup = true;
          break;
        }
      }
      if (!dup) out.push_back(path);
    }
  }
  return out;
}

// Что реально передано движку в этом запуске: заполняет browser_app.cc до
// старта CEF, читает shell.cc — интерфейс показывает это как факт, без
// обещаний «движок загрузил».
inline std::vector<std::string>& ExtEnginePassed() {
  static std::vector<std::string> passed;
  return passed;
}

inline std::vector<std::string> ExtEnginePassedIds() {
  std::vector<std::string> ids;
  for (const auto& dir : ExtEnginePassed()) {
    const std::string id = ExtDirName(dir);
    if (!id.empty()) ids.push_back(id);
  }
  return ids;
}

}  // namespace shelter

#endif  // SHELTER_EXTENSION_ENGINE_H_
