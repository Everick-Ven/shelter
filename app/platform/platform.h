#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace shelter {
// Native platform services used by the JS bridge (UI thread only).
std::optional<std::string> PlatformClipboardRead();
bool PlatformClipboardWrite(std::string_view text);
std::string PlatformDefaultDownloadsDir();
}  // namespace shelter
