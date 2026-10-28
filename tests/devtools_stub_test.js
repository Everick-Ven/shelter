'use strict';

// The user-facing DevTools entry points are intentionally a harmless UI stub.
// They must never construct a webview or ask the native CEF host to open DevTools.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const html = read('resources/ui/index.html');
const hostBridge = read('resources/ui/host-bridge.js');
const bridge = read('src/shell_bridge.cc');
const clients = read('src/clients.cc');
const shell = read('src/shell.cc');
const geometry = read('tools/check_geometry.py');
function sourceFiles(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const fullPath = path.join(directory, entry.name);
    return entry.isDirectory() ? sourceFiles(fullPath) : [fullPath];
  });
}
const nativeSources = sourceFiles(path.join(root, 'src'))
  .map(file => fs.readFileSync(file, 'utf8'))
  .join('\n');

function expectMatch(source, pattern, message) {
  assert.ok(pattern.test(source), message || `expected source to match ${pattern}`);
}
function expectNoMatch(source, pattern, message) {
  assert.ok(!pattern.test(source), message || `expected source not to match ${pattern}`);
}
function section(source, start, end, label) {
  const i = source.indexOf(start);
  const j = source.indexOf(end, i + start.length);
  assert.ok(i >= 0 && j > i, `${label} section exists`);
  return source.slice(i, j);
}

// Dock, close control, title and static placeholder.
expectMatch(html, /\.stage\.dt-open \.viewport\s*\{/,
  'opening the stub reserves room for it');
expectMatch(html, /\.stage\.dt-open \.devtools\s*\{/,
  'the stub panel follows the stage open state');
expectMatch(html, /body\[data-gfx="perf"\][\s\S]*?\.devtools/,
  'the performance theme has a DevTools-panel override');
expectMatch(html, /body\.native-content-visible:not\(\[data-gfx="perf"\]\)[\s\S]*?\.devtools/,
  'the native-content theme has a DevTools-panel override');
expectMatch(html, /<div class="devtools" id="devtools"[\s\S]*?<span class="dt-own" id="dtOwner"/);
expectMatch(html, /<div class="dt-b" id="dtBody"[\s\S]*?<div class="dt-stub" id="dtStub"/);
const placeholder = section(html, '<div class="dt-stub" id="dtStub" hidden>', '</div>', 'static placeholder');
expectMatch(placeholder, /<span>Встроенные инструменты разработчика сейчас недоступны\.<\/span>/,
  'the panel body contains one static unavailable message');
expectNoMatch(placeholder, /<b\b|<p\b|<strong\b/,
  'the placeholder has no second text block');
expectMatch(html, /data-act="devtoolsClose"/);
expectNoMatch(html, /<webview\b|setDevToolsWebContents|getDevToolsRect|devtools\/layout/i,
  'the UI contains no webview, old DevTools contents API, geometry hook, or layout IPC');

// Keep the Windows buttons in the toolbar, after tb-actions and outside the stage.
const header = section(html, '<header class="tb">', '</header>', 'toolbar');
assert.ok(header.indexOf('class="tb-actions"') < header.indexOf('id="winCtl"'),
  '#winCtl remains after .tb-actions');
const stage = section(html, '<section class="stage" id="stage">', '</section>', 'stage');
expectNoMatch(stage, /id="winCtl"/, '#winCtl is not moved into or covered by the stub panel');
expectMatch(html, /#winCtl\.win-ctls\{[^}]*position:fixed[^}]*z-index:40[^}]*-webkit-app-region:no-drag/,
  '#winCtl remains fixed above the panel and excluded from drag hit-testing');

// Menu preference, developer persona, page context action and shortcuts.
expectMatch(html, /menuAdapt:\s*true,\s*menuDev:\s*true/,
  'the visibility preference defaults to enabled');
expectMatch(html, /menuDev:\s*\[[^\]]*DevTools-пункт показан в меню/);
expectMatch(html, /const PERS_K = \[[^\]]*'menuDev'/);
expectMatch(html, /'pers-dev':\s*\{[^\n]*p:\s*\{[^\n]*menuDev:\s*true/,
  'the developer persona retains the DevTools menu item');
expectMatch(html, /row\('Инструменты разработчика в меню «\.\.\.»',\s*'[^\n]*menuDev/);
const moreMenu = section(html, 'function moreMenu(anchor) {', 'const AUTH_PROVIDERS', 'more menu');
expectMatch(moreMenu, /S\.prefs\.menuDev[\s\S]*label: 'Инструменты разработчика'[\s\S]*kbd: IS_MAC \? '⌥⌘I' : 'F12'[\s\S]*toggleDevtools/);
const contextMenu = section(html, 'window.shelterCtxMenu = function (d) {', "\ndocument.addEventListener('contextmenu'", 'page context menu');
expectMatch(contextMenu, /S\.prefs\.menuDev[\s\S]*label: 'Инспектировать'[\s\S]*toggleDevtools/);
expectNoMatch(contextMenu, /act\(['"]inspect['"]|cefSend|shelterCefRequest/,
  'Inspect opens only the local stub');
const shortcuts = html.match(/const SHORTCUTS = \[[^\n]+/);
assert(shortcuts && shortcuts[0].includes("['Инструменты разработчика', '⌥⌘I']"),
  'the shortcut help retains the macOS DevTools shortcut');
const keydown = section(html, "document.addEventListener('keydown', e => {\n  const mod =", "document.addEventListener('visibilitychange'", 'keyboard shortcuts');
expectMatch(keydown, /e\.key === 'F12'[\s\S]*toggleDevtools/,
  'F12 opens/closes the stub');
expectMatch(keydown, /K === 'KeyI' && e\.shiftKey[\s\S]*toggleDevtools/,
  'Ctrl/Cmd+Shift+I opens the stub');
expectMatch(keydown, /K === 'KeyI' && e\.altKey && e\.metaKey[\s\S]*toggleDevtools/,
  'Alt+Cmd+I opens the stub on macOS');

// The panel has local open/close state and an active-tab title, but no native calls.
const stubCode = section(html, 'function dtUpdateHeader(', 'function setDevice(', 'DevTools stub lifecycle');
expectMatch(stubCode, /function dtUpdateHeader\(tab\)/);
expectMatch(stubCode, /dtOwner[\s\S]*textContent[\s\S]*selected\.title/,
  'the panel title follows the selected tab');
expectMatch(html, /function renderView\([\s\S]*?dtUpdateHeader\(t\)/,
  'tab selection updates the stub title');
expectMatch(html, /window\.shelterUpdateTitle = \([\s\S]*?dtUpdateHeader\(tab\)/,
  'native title updates are reflected in the stub header');
expectMatch(stubCode, /function toggleDevtools\(force\)/);
expectMatch(stubCode, /classList\.toggle\('dt-open', open\)/);
expectMatch(stubCode, /dtStub[\s\S]*hidden = !open/);
expectNoMatch(stubCode, /NAT\.|cefSend|shelterCefRequest|shelterCef\.|devtools\/layout/,
  'opening and closing the stub has no IPC or native side effects');
expectNoMatch(hostBridge, /devtools|DevTools/i,
  'the host bridge does not transport DevTools UI or layout operations');

// A stale or reintroduced bridge command is explicitly refused without side effects.
const handleBridge = section(bridge, 'bool Shell::HandleBridge(', '// ---- действия над вкладкой', 'native bridge handler');
const denied = handleBridge.match(/if \(m == "devtools" \|\| m == "devtools\.ctl"\)\s*\{([\s\S]*?)\n  \}/);
assert(denied, 'the compatibility bridge refuses both DevTools method names');
assert(denied[1].includes('cb->Success("{\\"ok\\":false}");') && denied[1].includes('return true;'),
  'the refused bridge request returns exactly {"ok":false}');
assert.ok(handleBridge.indexOf('if (m == "devtools"') < handleBridge.indexOf('if (!IsUiBrowser(browser))'),
  'DevTools aliases are denied before any other bridge handling');
expectNoMatch(denied[1], /SetDevTools|TabAction|UiEvent|ExecuteDevToolsMethod/,
  'the refused bridge request does not perform native work');
const legacyBridge = section(clients, 'if (!d->HasKey("m")) {', 'const std::string method', 'legacy bridge handler');
const legacyDenied = legacyBridge.match(/if \(cmd == "devtools" \|\| cmd == "devtools\.ctl" \|\| cmd == "devtools:ctl"\)\s*\{([\s\S]*?)\n      \}/);
assert(legacyDenied, 'legacy devtools bridge aliases are explicitly refused');
assert(legacyDenied[1].includes('callback->Success("{\\"ok\\":false}");') && legacyDenied[1].includes('return true;'),
  'the legacy bridge returns {"ok":false} instead of the default success response');
expectNoMatch(legacyDenied[1], /Shell::Get|TabAction|UiEvent|devTools/,
  'the legacy bridge refusal has no native side effects');
const tabAction = section(bridge, 'void Shell::TabAction(', 'void Shell::SetZoomAll(', 'native tab action');
expectNoMatch(tabAction, /act == "devtools"|act == "inspect"/,
  'native tab actions cannot open or inspect DevTools');
const keyStart = shell.indexOf('bool Shell::OnTabKey(');
const keyEnd = shell.indexOf('\nvoid Shell::', keyStart + 1);
assert.ok(keyStart >= 0 && keyEnd > keyStart, 'native tab key handler exists');
expectNoMatch(shell.slice(keyStart, keyEnd), /F12|KeyI|devtools/i,
  'the native page-key handler does not intercept DevTools shortcuts');
expectNoMatch(nativeSources, /ShowDevTools|CloseDevTools|ui-devtools/,
  'native code has no DevTools frontend APIs or hidden keyboard switch');
expectMatch(nativeSources, /ExecuteDevToolsMethod/,
  'internal CDP-backed snapshot/cache features remain available');
expectMatch(nativeSources, /SnapObserver/,
  'the internal CDP snapshot observer remains available');
expectMatch(nativeSources, /AddDevToolsMessageObserver/,
  'the internal snapshot observer remains available');
expectMatch(geometry, new RegExp(['--remote', 'debugging', 'port='].join('-')),
  'CI retains its remote debugging protocol endpoint');

console.log('In-app DevTools stub and native-denial regression tests passed');
