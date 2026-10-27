'use strict';

// DevTools opens from a UI request after the dock geometry is applied. The CEF
// call remains deferred and owns a strong BrowserHost reference; stale tab
// requests are ignored after close/switch.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '..', 'src', 'shell_bridge.cc'), 'utf8');
const start = source.indexOf('void Shell::TabAction(');
const end = source.indexOf('\nvoid Shell::SetZoomAll(', start);
assert(start >= 0 && end > start, 'TabAction implementation must be present');
const tabAction = source.slice(start, end);

assert.match(tabAction, /act == "devtools"[\s\S]*?UiEvent\("devtools\.request"/,
  'native F12/context entry points request the dock UI rather than opening a window directly');
assert.match(tabAction, /act == "inspect"[\s\S]*?UiEvent\("devtools\.request"[\s\S]*?std::to_string\(std::max\(0, x\)\)/,
  'Inspect preserves the clicked element coordinates in the dock request');
assert.doesNotMatch(tabAction, /PostShowDevTools\(|->ShowDevTools\(/,
  'TabAction must not enter CEF ShowDevTools from a browser/router callback');
assert.match(source, /CefPostTask\(TID_UI, new ShowDevToolsTask/,
  'the browser-process UI task queue performs the CEF operation');
assert.match(source, /CefRefPtr<CefBrowserHost> host_/,
  'the task keeps a strong host reference through ShowDevTools');
assert.match(source, /CanShowDevTools\(tab_id_, browser_->GetIdentifier\(\),\s*generation_\)/,
  'stale tasks are gated against the current target and generation');
assert.match(source, /host_->ShowDevTools\(/,
  'the queued task owns the actual CEF ShowDevTools call');
assert.match(source, /OnDevToolsShowCallReturned\(tab_id_, generation_\)/,
  'the opening state is released if CEF does not synchronously create the popup');

console.log('DevTools bridge reentrancy regression test passed');
