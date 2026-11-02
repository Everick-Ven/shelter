'use strict';

const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const clients = read('src/clients.cc');
const windows = read('src/platform_win.cc');
const mac = read('src/platform_mac.mm');
const platform = read('src/platform.h');
const helper = read('src/communication_app_url.h');

const from = (text, start, end) => {
  const begin = text.indexOf(start);
  assert.notEqual(begin, -1, `missing source section: ${start}`);
  const finish = text.indexOf(end, begin + start.length);
  assert.notEqual(finish, -1, `missing source boundary: ${end}`);
  return text.slice(begin, finish);
};

for (const scheme of [
  'tg', 'sgnl', 'slack', 'msteams', 'zoommtg', 'webextel',
  'matrix', 'facetime', 'mailto', 'tel', 'sms'
]) {
  assert.ok(helper.includes(`"${scheme}"`), `missing allowlisted protocol: ${scheme}`);
}
for (const scheme of [
  'whatsapp', 'discord', 'line', 'weixin', 'webex', 'ms-teams', 'skype',
  'zoomus', 'viber', 'facetime-audio', 'telemost', 'gotomeeting',
  'jitsi-meet', 'threema', 'wire', 'mattermost',
  'rocketchat', 'kakaotalk', 'callto'
]) {
  assert.ok(!helper.includes(`"${scheme}"`), `unexpected allowlisted protocol: ${scheme}`);
}
assert.match(helper, /kMaxUrlBytes = 8192/);
assert.match(helper, /c <= 0x20 \|\| c == 0x7f/);
assert.match(helper, /UriSyntax::kOpaque/);

const tabBrowse = from(clients, 'bool TabClient::OnBeforeBrowse(',
                       '// ---- блокировка трекеров/рекламы');
assert.match(tabBrowse, /IsCommunicationAppProtocolUrl\(url\)/);
assert.match(tabBrowse, /frame && frame->IsMain\(\)/);
assert.match(tabBrowse, /!is_redirect\s*&&\s*user_gesture/);
assert.doesNotMatch(tabBrowse, /TT_TYPED|TT_CLIENT_REDIRECT_FLAG/,
                    'automatic redirects must not launch external handlers');
assert.match(tabBrowse, /DispatchCommunicationAppUrl\(url\)/);
assert.match(tabBrowse, /return true/);

const tabPopup = from(clients, 'bool TabClient::OnBeforePopup(',
                      'bool TabClient::OnOpenURLFromTab(');
assert.match(tabPopup, /IsCommunicationAppProtocolUrl\(url\)/);
assert.match(tabPopup, /if \(user_gesture\) DispatchCommunicationAppUrl\(url\)/);
const tabOpen = from(clients, 'bool TabClient::OnOpenURLFromTab(',
                     'void TabClient::OnAfterCreated(');
assert.match(tabOpen, /IsCommunicationAppProtocolUrl\(target\)/);
assert.match(tabOpen, /if \(user_gesture\) DispatchCommunicationAppUrl\(target\)/);

const popupBrowse = from(clients, 'bool PopupClient::OnBeforeBrowse(',
                         'bool PopupClient::OnOpenURLFromTab(');
assert.match(popupBrowse, /frame && frame->IsMain\(\)/);
assert.match(popupBrowse, /!is_redirect\s*&&\s*user_gesture/);
assert.doesNotMatch(popupBrowse, /TT_TYPED|TT_CLIENT_REDIRECT_FLAG/,
                    'automatic redirects must not launch external handlers');
assert.match(popupBrowse, /DispatchCommunicationAppUrl\(url\)/);
const uiBrowse = from(clients, 'bool UiClient::OnBeforeBrowse(',
                      'void UiClient::OnRenderProcessTerminated(');
assert.doesNotMatch(uiBrowse, /DispatchCommunicationAppUrl|LaunchCommunicationAppUrl/,
                    'external protocols are not dispatched from the privileged UI browser');

assert.match(platform, /bool LaunchCommunicationAppUrl\(const std::string& url\)/);
const windowsLaunch = from(windows, 'bool LaunchCommunicationAppUrl(',
                           'std::string ClipboardRead()');
assert.match(windowsLaunch, /IsCommunicationAppProtocolUrl\(url\)/);
assert.match(windowsLaunch, /ShellExecuteW/);
const macLaunch = from(mac, 'bool LaunchCommunicationAppUrl(',
                       'std::string ClipboardRead()');
assert.match(macLaunch, /IsCommunicationAppProtocolUrl\(url\)/);
assert.match(macLaunch, /NSWorkspace sharedWorkspace\][\s\S]*?openURL/);

const temporaryDirectory = fs.mkdtempSync(path.join(os.tmpdir(), 'shelter-communication-app-'));
try {
  const binary = path.join(temporaryDirectory, 'communication-app-protocol-test');
  execFileSync(process.env.CXX || 'c++', [
    '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fno-exceptions', '-I.',
    'tests/communication_app_protocol_test.cc', '-o', binary
  ], { cwd: root, stdio: 'inherit' });
  execFileSync(binary, [], { cwd: root, stdio: 'inherit' });
} finally {
  fs.rmSync(temporaryDirectory, { recursive: true, force: true });
}

console.log('Communication-app protocol dispatch and allowlist checks passed');
