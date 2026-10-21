// SHELTER — преобразование путей между UTF-8 интерфейсами и файловой системой ОС.
#ifndef SHELTER_PATH_UTILS_H_
#define SHELTER_PATH_UTILS_H_

#include <filesystem>
#include <string>

namespace shelter {
namespace path_utils {

// Все пути std::string внутри приложения хранятся в UTF-8.
inline std::filesystem::path PathFromUtf8(const std::string& value) {
  return std::filesystem::u8path(value);
}

// Возвращает UTF-8 независимо от типа char8_t в стандарте C++.
inline std::string PathToUtf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
#if defined(__cpp_char8_t)
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
  return value;
#endif
}

}  // namespace path_utils
}  // namespace shelter

#endif  // SHELTER_PATH_UTILS_H_
