// Explicitly allowlisted communication-app and system communication URLs.
#ifndef SHELTER_COMMUNICATION_APP_URL_H_
#define SHELTER_COMMUNICATION_APP_URL_H_

#include <cstddef>
#include <string>

namespace shelter {
namespace communication_app_url_detail {

enum class UriSyntax { kAuthority, kOpaque, kEither };

struct SchemeRule {
  const char* scheme;
  UriSyntax syntax;
};

inline bool IsAsciiAlpha(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline bool IsSchemeChar(unsigned char c) {
  return IsAsciiAlpha(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' ||
         c == '.';
}

inline unsigned char LowerAscii(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

inline bool SchemeEquals(const std::string& url, std::size_t length,
                         const char* expected) {
  std::size_t expected_length = 0;
  while (expected[expected_length] != '\0') ++expected_length;
  if (length != expected_length) return false;
  for (std::size_t i = 0; i < length; ++i) {
    if (LowerAscii(static_cast<unsigned char>(url[i])) !=
        static_cast<unsigned char>(expected[i])) {
      return false;
    }
  }
  return true;
}

inline bool FindSchemeRule(const std::string& url, std::size_t length,
                           UriSyntax* syntax) {
  // Explicitly documented app handlers; see docs/communication-app-uri-schemes.md.
  constexpr SchemeRule kAuthoritySchemes[] = {
      {"facetime", UriSyntax::kAuthority},
      {"msteams", UriSyntax::kAuthority},
      {"tg", UriSyntax::kAuthority},
      {"sgnl", UriSyntax::kAuthority},
      {"slack", UriSyntax::kAuthority},
      {"webextel", UriSyntax::kAuthority},
      {"zoommtg", UriSyntax::kAuthority},
      {"zoomus", UriSyntax::kAuthority},
  };
  // System-level communication and the Matrix URI scheme.
  constexpr SchemeRule kOpaqueSchemes[] = {
      {"mailto", UriSyntax::kOpaque},
      {"tel", UriSyntax::kOpaque},
      {"sms", UriSyntax::kOpaque},
  };
  // Matrix permits both matrix:... and matrix://authority/... forms.
  constexpr SchemeRule kEitherSchemes[] = {
      {"matrix", UriSyntax::kEither},
  };

  for (const SchemeRule& rule : kAuthoritySchemes) {
    if (SchemeEquals(url, length, rule.scheme)) {
      *syntax = rule.syntax;
      return true;
    }
  }
  for (const SchemeRule& rule : kOpaqueSchemes) {
    if (SchemeEquals(url, length, rule.scheme)) {
      *syntax = rule.syntax;
      return true;
    }
  }
  for (const SchemeRule& rule : kEitherSchemes) {
    if (SchemeEquals(url, length, rule.scheme)) {
      *syntax = rule.syntax;
      return true;
    }
  }
  return false;
}

inline bool HasAuthority(const std::string& url, std::size_t colon) {
  return colon + 2 < url.size() && url[colon + 1] == '/' &&
         url[colon + 2] == '/';
}

inline bool HasValidAuthority(const std::string& url, std::size_t colon) {
  if (!HasAuthority(url, colon) || colon + 3 >= url.size()) return false;
  const char first = url[colon + 3];
  return first != '/' && first != '?' && first != '#';
}

}  // namespace communication_app_url_detail

// Arbitrary URI schemes can launch local programs. Only the protocols above
// are passed to the OS; controls, whitespace, malformed forms, and oversized
// values are rejected before ShellExecute/LaunchServices sees them.
inline bool IsCommunicationAppProtocolUrl(const std::string& url) {
  constexpr std::size_t kMaxUrlBytes = 8192;
  if (url.empty() || url.size() > kMaxUrlBytes) return false;
  for (unsigned char c : url) {
    if (c <= 0x20 || c == 0x7f) return false;
  }

  const std::size_t colon = url.find(':');
  if (colon == std::string::npos || colon == 0 || colon > 32) return false;
  const unsigned char first = static_cast<unsigned char>(url[0]);
  if (!communication_app_url_detail::IsAsciiAlpha(first)) return false;
  for (std::size_t i = 1; i < colon; ++i) {
    if (!communication_app_url_detail::IsSchemeChar(
            static_cast<unsigned char>(url[i]))) {
      return false;
    }
  }

  communication_app_url_detail::UriSyntax syntax;
  if (!communication_app_url_detail::FindSchemeRule(url, colon, &syntax))
    return false;
  const bool has_authority =
      communication_app_url_detail::HasAuthority(url, colon);
  if (has_authority) {
    if (syntax == communication_app_url_detail::UriSyntax::kOpaque) return false;
    return communication_app_url_detail::HasValidAuthority(url, colon);
  }
  if (syntax == communication_app_url_detail::UriSyntax::kAuthority ||
      colon + 1 >= url.size()) {
    return false;
  }
  const char first_part = url[colon + 1];
  return first_part != '/' && first_part != '?' && first_part != '#';
}

}  // namespace shelter

#endif  // SHELTER_COMMUNICATION_APP_URL_H_
