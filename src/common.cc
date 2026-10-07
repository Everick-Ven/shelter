#include "src/common.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "include/cef_parser.h"

namespace shelter {

std::string ToJson(CefRefPtr<CefValue> value) {
  if (!value) {
    return "null";
  }
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::string ToJson(CefRefPtr<CefDictionaryValue> dict) {
  CefRefPtr<CefValue> v = CefValue::Create();
  v->SetDictionary(dict);
  return ToJson(v);
}

CefRefPtr<CefValue> ParseJson(const std::string& json) {
  return CefParseJSON(json, JSON_PARSER_RFC);
}

std::string JsString(const std::string& s) {
  // Через CefValue -> JSON: получаем корректно экранированную строку.
  CefRefPtr<CefValue> v = CefValue::Create();
  v->SetString(s);
  std::string out = ToJson(v);
  // U+2028/2029 допустимы в современном JS, но заменим на всякий случай.
  std::string res;
  res.reserve(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    if (i + 2 < out.size() && static_cast<unsigned char>(out[i]) == 0xE2 &&
        static_cast<unsigned char>(out[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(out[i + 2]) == 0xA8 ||
         static_cast<unsigned char>(out[i + 2]) == 0xA9)) {
      res += (static_cast<unsigned char>(out[i + 2]) == 0xA8) ? "\\u2028"
                                                              : "\\u2029";
      i += 2;
    } else {
      res += out[i];
    }
  }
  return res;
}

std::string SanitizeForPath(const std::string& s) {
  std::string r;
  for (char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      r += c;
    } else {
      r += '_';
    }
  }
  if (r.empty()) {
    r = "default";
  }
  if (r.size() > 80) {
    r.resize(80);
  }
  return r;
}

std::string RegistrableDomain(const std::string& host_in) {
  auto lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  const std::string host = lower(host_in);
  static const char* const kTwoLevel[] = {
      "co.uk", "org.uk", "net.uk", "gov.uk", "ac.uk", "com.au", "net.au",
      "org.au", "co.nz", "co.jp", "ne.jp", "or.jp", "com.br", "com.mx",
      "com.ar", "com.tr", "com.pl", "com.ua", "net.ru", "org.ru", "com.ru",
      "pp.ru", "msk.ru", "spb.ru", "co.il", "co.kr", "co.in", "com.cn",
      "net.cn", "org.cn", "com.hk", "com.sg", "com.tw", "co.za", "co.id"};
  std::vector<std::string> labels;
  size_t start = 0;
  for (size_t i = 0; i <= host.size(); ++i) {
    if (i == host.size() || host[i] == '.') {
      if (i > start) labels.push_back(host.substr(start, i - start));
      start = i + 1;
    }
  }
  if (labels.size() <= 2) return host;
  const std::string last2 = labels[labels.size() - 2] + "." + labels.back();
  const bool two_level =
      std::find(std::begin(kTwoLevel), std::end(kTwoLevel), last2) !=
      std::end(kTwoLevel);
  const size_t take = two_level ? 3 : 2;
  std::string out;
  for (size_t i = labels.size() - take; i < labels.size(); ++i) {
    if (!out.empty()) out += ".";
    out += labels[i];
  }
  return out;
}

}  // namespace shelter
