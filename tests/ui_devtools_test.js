'use strict';

// DevTools (этап 1): настоящий Chromium DevTools в отдельном окне, без мёртвого
// Electron-кода дока. Проверяет UI, мост и нативные хоткеи.

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8');
const html = read('resources/ui/index.html');
const host = read('resources/ui/host-bridge.js');
const bridge = read('src/shell_bridge.cc');
const shell = read('src/shell.cc');

// --- UI: мёртвый <webview>/Electron-код дока удалён --------------------------
for (const dead of ['setDevToolsWebContents', 'dtHostEl', 'dtHostReady', 'dtHostPoll', 'dtSendDevOpen',
  'dtDevTarget', 'dtPanelOpen', 'dtPanelClose', "'devHost'", 'dt-host']) {
  assert(!html.includes(dead), `мёртвый код дока не должен вернуться: ${dead}`);
}
assert(!/toggleDevtools\(/.test(html), 'док-переключатель заменён openDevtools');

// --- UI: единая точка входа и проверки ----------------------------------------
assert.match(html, /function openDevtools\(\) \{\s*if \(!dtCanInspect\(\)\) return;\s*const p = dtDevSend\(\{ act: 'open' \}\);/);
assert.match(html, /m && m\.kind === 'internal'/, 'внутренние страницы SHELTER не инспектируются');
assert.match(html, /kbd: IS_MAC \? '⌥⌘I' : 'F12', run: \(\) => openDevtools\(\)/, 'пункт меню показывает платформенный хоткей');
assert.match(html, /devtools: \(\) => openDevtools\(\)/);

// --- UI: хоткеи ---------------------------------------------------------------
assert.match(html, /if \(e\.key === 'F12'\) \{ e\.preventDefault\(\); openDevtools\(\); return; \}/);
assert.match(html, /K === 'KeyI' && \(e\.shiftKey \|\| \(e\.altKey && e\.metaKey\)\)\) \{ e\.preventDefault\(\); openDevtools\(\); \}/,
  'Ctrl+Shift+I и ⌥⌘I; Ctrl+Alt+I (AltGr) не перехватывается');

// --- Контекстное меню страницы: «Инспектировать» открывает элемент под курсором ---
assert.match(html, /act\('inspect', \(loc\.x \| 0\) \+ ',' \+ \(loc\.y \| 0\)\)/);
assert.match(html, /if \(S\.prefs\.menuDev !== false\) \{\s*const loc = d\.local \|\| \{\};/, 'пункт подчиняется настройке menuDev');
assert.match(host, /local: \{ x: p\.x \|\| 0, y: p\.y \|\| 0 \}/, 'мост передаёт локальные координаты клика');

// --- Нативная часть ------------------------------------------------------------
assert.match(bridge, /\(act == "window" \|\| act == "open"\) && devtools_tab &&\s*devtools_tab->browser/);
assert.match(bridge, /\} else if \(act == "inspect"\) \{[\s\S]*?PostShowDevTools\(b,\s*CefPoint\(std::max\(0, x\), std::max\(0, y\)\)\)/,
  'Inspect still targets the clicked point, via the deferred CEF call');
assert.match(bridge, /#include <cstdlib>/, 'std::atoi требует <cstdlib>');
assert.match(shell, /code == "F12" \|\| \(mod && shift && code == "KeyI"\) \|\| mac_devtools/);
assert.match(shell, /#if defined\(OS_MAC\)\s*const bool mac_devtools = mod && alt && !shift && code == "KeyI";/);
assert.match(shell, /#else\s*const bool mac_devtools = false;/, 'на Windows Ctrl+Alt+I (AltGr) не перехватывается');

console.log('UI_DEVTOOLS_TEST_PASS');
