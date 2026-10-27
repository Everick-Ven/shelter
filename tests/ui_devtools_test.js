'use strict';

// DevTools stage 2: dock the real Chromium frontend as a CEF Views overlay,
// preserve bottom/right resizing, and retain the existing keyboard/context UI.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8');
const html = read('resources/ui/index.html');
const host = read('resources/ui/host-bridge.js');
const bridge = read('src/shell_bridge.cc');
const shell = read('src/shell.cc');
const header = read('src/shell.h');

// --- UI: the old Electron/webview frontend is still absent --------------------
for (const dead of ['setDevToolsWebContents', 'dtHostEl', 'dtHostReady', 'dtHostPoll', 'dtSendDevOpen',
  'dtDevTarget', 'dtPanelOpen', 'dtPanelClose', "'devHost'", 'dt-host']) {
  assert(!html.includes(dead), `dead Electron DevTools code must not return: ${dead}`);
}
assert(!/toggleDevtools\(/.test(html), 'the old popup toggle must not return');

// --- UI: dock geometry, resize persistence, actions and internal-page stub ----
assert.match(html, /function openDevtools\(options = \{\}\)/);
assert.match(html, /stage\.classList\.add\('dt-open'\)/);
assert.match(html, /act: 'dock'[\s\S]*?tabId:[\s\S]*?rect:/);
assert.match(html, /act: 'layout', rect: r, visible: state\.open/);
assert.match(html, /S\.ui\.devtoolsDock/);
assert.match(html, /S\.ui\.devtoolsHeight/);
assert.match(html, /S\.ui\.devtoolsWidth/);
assert.match(html, /data-act="devtoolsDockBottom"/);
assert.match(html, /data-act="devtoolsDockRight"/);
assert.match(html, /data-act="devtoolsUndock"/);
assert.match(html, /id="dtResize"/);
assert.match(html, /\.stage\.dt-open\.dt-right \.viewport/);
assert.match(html, /\.dt-resize[\s\S]*cursor:\s*ns-resize/);
assert.match(html, /\.stage\.dt-open\.dt-right \.dt-resize[\s\S]*cursor:\s*ew-resize/);
assert.match(html, /Внутренняя страница SHELTER/);
assert.match(html, /case 'devtools\.request'/);
assert.match(html, /case 'devtools\.closed'/);

const renderViewStart = html.indexOf('function renderView(');
const renderViewEnd = html.indexOf('function refreshPage(', renderViewStart);
assert(renderViewStart >= 0 && renderViewEnd > renderViewStart, 'active-tab renderer must be present');
assert.match(html.slice(renderViewStart, renderViewEnd), /dtUpdateHeader\(t\)/,
  'switching between web and shelter:// tabs updates the dock owner and internal-page stub');

// --- UI: platform shortcuts and native context inspection ----------------------
assert.match(html, /kbd: IS_MAC \? '⌥⌘I' : 'F12', run: \(\) => openDevtools\(\)/);
assert.match(html, /devtools: \(\) => openDevtools\(\)/);
assert.match(html, /if \(e\.key === 'F12'\) \{ e\.preventDefault\(\); openDevtools\(\); return; \}/);
assert.match(html, /K === 'KeyI' && \(e\.shiftKey \|\| \(e\.altKey && e\.metaKey\)\)\) \{ e\.preventDefault\(\); openDevtools\(\); \}/,
  'Ctrl+Shift+I and Option+Command+I remain available without intercepting AltGr');
assert.match(html, /act\('inspect', \(loc\.x \| 0\) \+ ',' \+ \(loc\.y \| 0\)\)/);
assert.match(host, /local: \{ x: p\.x \|\| 0, y: p\.y \|\| 0 \}/);

// --- Host bridge: DevTools rect has its own q({m,a}) route ---------------------
assert.match(host, /function syncDevToolsLayout\(force\)/);
assert.match(host, /q\('devtools', \{ act: 'layout', rect: rect, visible: visible \}\)/);
assert.match(host, /new ResizeObserver\(scheduleDevToolsLayout\)\.observe\(dtBody\)/);
assert.match(host, /q\('view\.hide', internalArgs \|\| undefined\)/);

// --- Native routing and Views lifecycle ---------------------------------------
assert.match(bridge, /act == "dock" \|\| act == "open" \|\| act == "window"/);
assert.match(bridge, /act == "layout"[\s\S]*?LayoutDevTools\(/);
assert.match(bridge, /act == "undock"[\s\S]*?UndockDevTools\(/);
assert.match(bridge, /act == "close"[\s\S]*?CloseDevToolsPanel\(/);
assert.match(bridge, /SetDevToolsTarget\(id, false, CefPoint\(\), false, false\)/,
  'active web-tab changes rebind DevTools without stealing focus');
assert.match(bridge, /SetDevToolsTarget\(id, true, CefPoint\(\), false, false\)/,
  'internal pages close the native frontend and keep the UI placeholder');
assert.match(bridge, /UiEvent\("devtools\.request"/);
assert.match(bridge, /CefRefPtr<CefBrowserHost> host_/);
assert.match(bridge, /host_->ShowDevTools\(/);
assert.match(bridge, /CefPostTask\(TID_UI, new ShowDevToolsTask/);
assert.match(shell, /is_devtools && Shell::Get\(\)\.OnDevToolsPopupBrowserViewCreated/);
assert.match(shell, /AddOverlayView\(\s*popup_browser_view, CEF_DOCKING_MODE_CUSTOM, true\)/);
assert.match(shell, /devtools_overlay_->SetBounds\(devtools_rect_\)/);
assert.match(shell, /OnBrowserDestroyed\(\s*CefRefPtr<CefBrowserView> browser_view/);
assert.match(shell, /PopupWindowDelegate\(view, true\)/);
assert.match(header, /CefRefPtr<CefOverlayController> devtools_overlay_/);
assert.match(header, /CefRect devtools_rect_/);

// No platform native-view/window-handle implementation is introduced.
assert.doesNotMatch(shell, /devtools.*(?:NSView|HWND)|(?:NSView|HWND).*devtools/i);
console.log('UI_DEVTOOLS_TEST_PASS');
