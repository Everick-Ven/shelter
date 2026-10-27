'use strict';

// Stage 2 acceptance: Chromium DevTools is a real CefBrowserView overlay,
// sized from the UI's dtBody rectangle, with per-tab lifetime and an undock path.
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

// CEF Views callback consumes DevTools popups in the main window; ordinary
// page popups keep their existing standalone-window behavior. DevTools itself
// must use Chrome runtime style even though SHELTER tabs use Alloy style.
const viewDelegate = header.slice(header.indexOf('class ShellBrowserViewDelegate'));
assert.match(viewDelegate, /GetDelegateForPopupBrowserView\([\s\S]*?bool is_devtools\)[\s\S]*?if \(is_devtools\)[\s\S]*?CEF_RUNTIME_STYLE_CHROME/);
assert.match(viewDelegate, /return new ShellBrowserViewDelegate\(CEF_RUNTIME_STYLE_CHROME\)/);
assert.match(viewDelegate, /return this;/);
assert.match(viewDelegate, /cef_runtime_style_t GetBrowserRuntimeStyle\(\) override \{\s*return runtime_style_;\s*\}/);
const mainWindowDelegate = shell.slice(
  shell.indexOf('class ShellWindowDelegate'),
  shell.indexOf('// Окно для popup')
);
assert.match(mainWindowDelegate, /GetWindowRuntimeStyle\(\) override \{[\s\S]*?return CEF_RUNTIME_STYLE_CHROME;/,
  'the dock host window must support the Chrome-style DevTools BrowserView');
const popupWindowDelegate = shell.slice(
  shell.indexOf('class PopupWindowDelegate'),
  shell.indexOf('// ---- расширения')
);
assert.match(popupWindowDelegate,
  /GetWindowRuntimeStyle\(\) override \{\s*return is_devtools_ \? CEF_RUNTIME_STYLE_CHROME\s*: CEF_RUNTIME_STYLE_ALLOY;/,
  'detached DevTools uses a Chrome-style window while ordinary popups stay Alloy');
assert.match(shell, /if \(is_devtools && Shell::Get\(\)\.OnDevToolsPopupBrowserViewCreated\(\s*browser_view, popup_browser_view\)\)/);
assert.match(shell, /AddOverlayView\(\s*popup_browser_view,\s*CEF_DOCKING_MODE_CUSTOM,\s*true\)/);
assert.match(shell, /devtools_overlay_->SetBounds\(devtools_rect_\)/);
assert.match(shell, /OnBrowserDestroyed\(\s*CefRefPtr<CefBrowserView> browser_view,\s*CefRefPtr<CefBrowser> browser\)/);
assert.match(header, /CefRefPtr<CefOverlayController> devtools_overlay_/);
assert.match(header, /CefRect devtools_rect_/);

// The host bridge sends the DevTools body rectangle as its own route. The
// legacy viewport:sync payload must not be the only source of native geometry.
assert.match(host, /function syncDevToolsLayout\(/);
assert.match(host, /q\('devtools',\s*\{\s*act:\s*'layout',\s*rect:/);
assert.match(host, /tabId: options\.tabId \|\| activeId\(\), internal: true/);
assert.match(host, /q\('view\.hide', internalArgs \|\| undefined\)/);

// Native routes dock/layout/close/undock and follows the active view or
// internal-page placeholder without using platform window handles.
assert.match(bridge, /act == "layout"[\s\S]*?LayoutDevTools\(/);
assert.match(bridge, /act == "undock"[\s\S]*?UndockDevTools\(/);
assert.match(bridge, /act == "close"[\s\S]*?CloseDevToolsPanel\(/);
assert.match(bridge, /SetDevToolsTarget\(id,\s*false,\s*CefPoint\(\),\s*false,\s*false\)/);
assert.match(bridge, /internal[\s\S]*?SetDevToolsTarget\(id,\s*true,\s*CefPoint\(\),\s*false,\s*false\)/);
assert.match(bridge, /void Shell::TabAction[\s\S]*?UiEvent\("devtools\.request"/);
assert.match(bridge, /host_->ShowDevTools\(/);
assert.match(bridge, /CefRefPtr<CefBrowserHost> host_/);
assert.doesNotMatch(bridge, /PostShowDevTools\(b,\s*CefPoint\(\)\)/,
  'native F12/context actions are routed through the dock UI request');

// UI has real dock/resize controls; sizes and side are persisted independently.
assert.match(html, /id="dtResize"/);
assert.match(html, /data-act="devtoolsDockBottom"/);
assert.match(html, /data-act="devtoolsDockRight"/);
assert.match(html, /data-act="devtoolsUndock"/);
assert.match(html, /S\.ui\.devtoolsDock/);
assert.match(html, /S\.ui\.devtoolsHeight/);
assert.match(html, /S\.ui\.devtoolsWidth/);
assert.match(html, /stage\.classList\.add\('dt-open'\)/);
assert.match(html, /act: 'dock'[\s\S]*?rect:/);
assert.match(html, /case 'devtools\.request'/);
assert.match(html, /internalPage|urlMeta\(t\.url\)[\s\S]*dtStub/);
assert.match(html, /\.stage\.dt-open\.dt-right \.viewport/);
assert.match(html, /\.dt-resize[\s\S]*cursor:\s*(?:ns|ew)-resize/);
assert.match(shell, /PopupWindowDelegate\(view,\s*true\)/);

console.log('DevTools dock stage-2 regression test passed');
