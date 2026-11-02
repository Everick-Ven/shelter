#include "src/communication_app_url.h"

#include <iostream>
#include <string>
#include <vector>

int main() {
  struct Case {
    const char* url;
    bool expected;
  };
  const std::vector<Case> cases = {
      // Documented handlers and URI forms observed in invite flows.
      {"zoommtg://zoom.us/join?action=join&confno=123456789", true},
      {"zoomus://us05web.zoom.us/join?action=join&confno=123456789"
       "&pwd=redacted&confid=redacted&browser=chrome",
       true},
      {"telemost://https//telemost.yandex.ru/j/123456789", true},
      {"msteams://teams.microsoft.com/l/meetup-join/123", true},
      {"webextel://login?telephone=123456789", true},
      // Explicitly documented messenger protocols.
      {"tg://resolve?domain=publicgroup", true},
      {"tg://resolve?domain=example_bot&start=payload", true},
      {"sgnl://signal.me/#p/+15551234567", true},
      {"slack://open?team=T123456", true},
      {"matrix:r/somewhere:example.org?action=join", true},
      {"matrix:u/alice:example.org?action=chat", true},
      {"matrix://matrix.example.org/r/somewhere:example.org?action=join", true},
      // OS communication links; opaque schemes do not use ://.
      {"facetime://person@example.org", true},
      {"mailto:invite@example.org?subject=Meeting", true},
      {"tel:+15551234567", true},
      {"sms:+15551234567", true},
      // HTTPS universal/app links remain ordinary browser navigations.
      {"https://t.me/publicgroup", false},
      {"https://wa.me/15551234567", false},
      {"https://signal.me/#p/+15551234567", false},
      // Unverified, deprecated, or non-allowlisted schemes stay in Chromium.
      {"whatsapp://send?phone=15551234567", false},
      {"discord://-/invite/inviteCode", false},
      {"line://ti/g/groupInvite", false},
      {"weixin://dl/chat?username=publicaccount", false},
      {"webex://meetings.webex.com/meet/host", false},
      {"ms-teams://teams.microsoft.com/l/meetup-join/123", false},
      {"zoomus://", false},
      {"viber://pa?chatURI=examplebot", false},
      {"facetime-audio://person@example.org", false},
      {"unknown-chat://host/room", false},
      {"javascript://example.org/alert(1)", false},
      {"file:///C:/secret.txt", false},
      {"mailto://person@example.org", false},
      {"intent://resolve#Intent;scheme=tg;end", false},

      {"zoommtg:/zoom.us/join?confno=123456789", false},
      {"zoommtg:///join?confno=123456789", false},
      {"zoommtg://", false},
      {"facetime:+15551234567", false},
      {"zoomus:///", false},
      {"zoomus:launch", false},
      {"telemost://", false},
      {"telemost:///", false},
      {"telemost:join", false},
      {"zoommtg://zoom.us/join\n?confno=123456789", false},
      {"zoommtg://zoom.us/join\t?confno=123456789", false},
      {"zoommtg-eval://zoom.us/join", false},
      {"skype:/echo123?call", false},
  };

  for (const auto& test : cases) {
    const bool actual = shelter::IsCommunicationAppProtocolUrl(test.url);
    if (actual != test.expected) {
      std::cerr << "communication app protocol check failed for: " << test.url
                << " (expected " << test.expected << ", got " << actual << ")\n";
      return 1;
    }
  }

  std::string oversized = "tg://resolve?domain=";
  oversized.append(8192, 'a');
  if (shelter::IsCommunicationAppProtocolUrl(oversized)) {
    std::cerr << "oversized protocol URL was accepted\n";
    return 1;
  }

  std::cout << "communication app protocol allowlist checks passed\n";
  return 0;
}
