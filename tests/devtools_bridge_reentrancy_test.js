'use strict';

// CEF's ShowDevTools API is dispatched off the UI task and then posts its
// internal work back to UI. This avoids synchronous popup creation in the
// browser/router callback and retains the stale-request guard.
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
  'the browser-process UI task validates the current target first');
assert.match(source, /CefRefPtr<CefBrowserHost> host_/,
  'the dispatched CEF task keeps a strong host reference');
assert.match(source, /CanShowDevTools\(tab_id_, browser_->GetIdentifier\(\),\s*generation_\)/,
  'stale tasks are gated against the current target and generation');

const callStart = source.indexOf('class ShowDevToolsCallTask');
const callEnd = source.indexOf('\nclass ShowDevToolsTask', callStart);
assert(callStart >= 0 && callEnd > callStart, 'off-UI CEF call task must be present');
const callTask = source.slice(callStart, callEnd);
assert.match(source, /CefPostTask\(\s*TID_FILE_USER_VISIBLE,\s*new ShowDevToolsCallTask/,
  'the UI task must defer the thread-safe CEF entry point off the UI call stack');
assert.match(callTask, /CEF_REQUIRE_FILE_USER_VISIBLE_THREAD\(\)/,
  'the CEF entry point must run on the intended CEF worker thread');
assert.match(callTask, /host_->ShowDevTools\(/,
  'the worker task owns the actual CEF ShowDevTools call');
assert.match(callTask, /CefPostTask\(TID_UI,\s*new ShowDevToolsReturnedTask/,
  'completion is reported back to UI after CEF has queued its internal work');
assert.match(source, /OnDevToolsShowCallReturned\(tab_id_, generation_, false\)/,
  'a stale or rejected request releases opening state without claiming a call');
const returnedStart = source.indexOf('class ShowDevToolsReturnedTask');
const returnedEnd = source.indexOf('\nclass ShowDevToolsCallTask', returnedStart);
assert(returnedStart >= 0 && returnedEnd > returnedStart, 'UI completion task must be present');
assert.match(callTask,
  /new ShowDevToolsReturnedTask\(\s*tab_id_, generation_, true\)/,
  'a completed CEF API call reports success back to UI');
assert.match(source.slice(returnedStart, returnedEnd),
  /OnDevToolsShowCallReturned\(tab_id_, generation_, did_call_\)/,
  'the UI completion task reports the result for CI verification');

console.log('DevTools bridge reentrancy regression test passed');
