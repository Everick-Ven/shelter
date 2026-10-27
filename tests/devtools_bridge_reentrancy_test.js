'use strict';

// Regression for the macOS crash report: never create/focus DevTools while a
// cefQuery callback is still dispatching Shell::TabAction. The CEF 154 wrapper
// enters DevTools synchronously on the UI thread, so window creation is posted
// until the bridge callback has returned.

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '..', 'src', 'shell_bridge.cc'), 'utf8');
const start = source.indexOf('void Shell::TabAction(');
const end = source.indexOf('\nvoid Shell::SetZoomAll(', start);
assert(start >= 0 && end > start, 'TabAction implementation must be present');
const tabAction = source.slice(start, end);

assert.match(tabAction, /act == "devtools"[\s\S]*?PostShowDevTools\(b,\s*CefPoint\(\)\)/,
  'ordinary DevTools opening must use the deferred path');
assert.match(tabAction, /act == "inspect"[\s\S]*?PostShowDevTools\(b,\s*CefPoint\(std::max\(0, x\),\s*std::max\(0, y\)\)\)/,
  'Inspect must preserve its point while using the deferred path');
assert.doesNotMatch(tabAction, /->ShowDevTools\(/,
  'TabAction must not enter CEF ShowDevTools re-entrantly from the bridge callback');
assert.match(source, /CefPostTask\(TID_UI,\s*new ShowDevToolsTask\(/,
  'the browser-process UI task queue must perform the DevTools operation');
assert.match(source, /class ShowDevToolsTask\s*:\s*public CefTask[\s\S]*?void Execute\(\) override[\s\S]*?ShowDevTools\(/,
  'the queued task owns the actual CEF ShowDevTools call');
assert.match(source, /if \(!browser_\s*\|\|\s*!browser_->IsValid\(\)\) return;/,
  'a tab closed before the queued task runs must be ignored safely');

console.log('DevTools bridge reentrancy regression test passed');
