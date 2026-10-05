#include "app/common/url_utils.h"

#include <algorithm>
#include <cctype>

namespace shelter {
namespace {

bool EqualsAsciiCaseInsensitive(std::string_view value, std::string_view expected) {
  if (value.size() != expected.size()) return false;
  return std::equal(value.begin(), value.end(), expected.begin(),
                    [](unsigned char left, unsigned char right) {
                      return std::tolower(left) == std::tolower(right);
                    });
}

}  // namespace

bool IsValidNavigationUrl(std::string_view url) {
  if (url.empty() || url.find_first_of("\r\n") != std::string_view::npos ||
      url.find('\0') != std::string_view::npos) {
    return false;
  }

  const size_t separator = url.find("://");
  if (separator == std::string_view::npos) return false;
  const std::string_view scheme = url.substr(0, separator);
  return EqualsAsciiCaseInsensitive(scheme, "http") ||
         EqualsAsciiCaseInsensitive(scheme, "https") ||
         EqualsAsciiCaseInsensitive(scheme, "shelter");
}

}  // namespace shelter
