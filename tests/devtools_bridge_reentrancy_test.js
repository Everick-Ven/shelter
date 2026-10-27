'use strict';

// DevTools opens from a deferred UI task after the router callback has returned.
// The call retains the host, checks the active target/generation, and reports
// whether the CEF entry point actually returned.
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
  'the browser-process UI task queue performs the deferred CEF operation');

const taskStart = source.indexOf('class ShowDevToolsTask');
const taskEnd = source.indexOf('\nvoid PostShowDevTools', taskStart);
assert(taskStart >= 0 && taskEnd > taskStart, 'deferred ShowDevTools task must be present');
const task = source.slice(taskStart, taskEnd);
assert.match(task, /CEF_REQUIRE_UI_THREAD\(\)/,
  'ShowDevTools is invoked on the CEF UI thread');
assert.match(task, /CefRefPtr<CefBrowserHost> host_/,
  'the task keeps a strong BrowserHost reference through ShowDevTools');
assert.match(task, /CanShowDevTools\(tab_id_, browser_->GetIdentifier\(\),\s*generation_\)/,
  'stale tasks are gated against the current target and generation');
assert.match(task, /host_->ShowDevTools\(/,
  'the queued task owns the actual CEF ShowDevTools call');
assert.match(task, /OnDevToolsShowCallReturned\(tab_id_, generation_, false\)/,
  'a stale task releases opening state without claiming a CEF call');
assert.match(task, /OnDevToolsShowCallReturned\(tab_id_, generation_, true\)/,
  'a returned CEF call releases opening state and can be verified in CI');

console.log('DevTools bridge reentrancy regression test passed');
