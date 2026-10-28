'use strict';

// Regression guard: the in-app DevTools frontend and every user-facing entry
// point stay removed. CEF's DevTools protocol is retained only for internal
// automation and existing screenshot/cache operations.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const html = read('resources/ui/index.html');
const hostBridge = read('resources/ui/host-bridge.js');
const shell = read('src/shell.cc');
const shellHeader = read('src/shell.h');
const bridge = read('src/shell_bridge.cc');
const clients = read('src/clients.cc');
const clientsHeader = read('src/clients.h');
const geometry = read('tools/check_geometry.py');
const workflow = read('.github/workflows/build.yml');
const readme = read('README.md');

assert.doesNotMatch(html, /id="devtools"|class="devtools"|data-act="devtools(?:DockBottom|DockRight|Undock|Close)"/,
  'the dock panel and its controls are absent');
assert.doesNotMatch(html, /function (?:openDevtools|closeDevtools|undockDevtools|setDevtoolsDock|initDevtoolsDock)\b|shelterOpenDevtools|shelterDevtoolsClosed|toggleDevtools|devToolsRect|getDevToolsRect/,
  'no UI DevTools lifecycle or public hook remains');
assert.doesNotMatch(html, /menuDev|Инструменты разработчика|Инспектировать|devtools\.request|devtools\.closed/,
  'menus, settings, context actions, and event hooks do not expose DevTools');
assert.doesNotMatch(html, /openDevtools\(|openDevtools\s*\}|\bF12\b|⌥⌘I/,
  'DevTools shortcuts and actions are removed');
assert.doesNotMatch(hostBridge, /syncDevToolsLayout|scheduleDevToolsLayout|devTools:\s*function|q\('devtools'/,
  'the host bridge no longer transports a dock route or rectangle');
assert.doesNotMatch(shellHeader, /SetDevToolsTarget|MaybeOpenDevToolsForTab|CanShowDevTools|OnDevToolsShowCallReturned|LayoutDevTools|CloseDevToolsPanel|UndockDevTools|OnDevToolsPopupBrowserViewCreated|devtools_(?:open|rect|target|view|overlay)/,
  'the Shell has no DevTools dock lifecycle state');
assert.doesNotMatch(shell, /ShowDevTools|CloseDevTools|OnDevToolsPopupBrowserViewCreated|devtools_(?:open|rect|target|view|overlay)|act == "devtools"|act == "inspect"/,
  'the native Shell cannot open, dock, inspect, or close a DevTools frontend');
assert.doesNotMatch(bridge, /ShowDevTools|CloseDevTools|SetDevToolsTarget|MaybeOpenDevToolsForTab|if \(m == "devtools"\)|act == "devtools"|act == "inspect"/,
  'the native bridge has no DevTools frontend route');
assert.doesNotMatch(clients + clientsHeader, /ShowDevTools|ui-devtools/,
  'the UI and tab keyboard handlers do not retain a hidden DevTools opener');
assert.doesNotMatch(geometry, /shelterOpenDevtools|ShowDevTools|DevTools Inspect|DevTools dock/,
  'geometry acceptance does not attempt to open the removed frontend');
assert.doesNotMatch(workflow, /ui_devtools_test|devtools_bridge_reentrancy_test|devtools_client_test|devtools_dock_test|native_devtools_diagnostics|native-devtools-diag|SHELTER_ATTACH_LLDB_ON_DEVTOOLS/,
  'CI no longer runs or dispatches the removed DevTools acceptance');
assert.doesNotMatch(readme, /F12\s*→\s*DevTools/,
  'README does not advertise an in-app DevTools window');

// CEF DevTools protocol remains an internal implementation detail for
// screenshots/cache operations and CDP-driven CI UI automation.
assert.match(bridge, /ExecuteDevToolsMethod/,
  'existing internal CDP-backed browser features remain available');
assert.match(geometry, new RegExp(['--remote', 'debugging', 'port='].join('-')),
  'CI can still drive the app through the local debugging protocol');

console.log('In-app DevTools removal regression test passed');
