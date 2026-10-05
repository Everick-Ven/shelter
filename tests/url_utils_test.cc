#include "app/common/url_utils.h"

#include <cassert>
#include <string>

int main() {
  assert(shelter::IsValidNavigationUrl("https://example.com"));
  assert(shelter::IsValidNavigationUrl("HTTPS://example.com"));
  assert(!shelter::IsValidNavigationUrl("javascript:alert(1)"));
  assert(!shelter::IsValidNavigationUrl("https://x\n"));
  const std::string nul_url("https://example.com\0.evil", 25);
  assert(!shelter::IsValidNavigationUrl(nul_url));
}
