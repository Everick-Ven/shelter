'use strict';

// CEF lifecycle-contract guard: BrowserApp has no GetDefaultClient(), so a null
// client would leave the popup unmanaged. This source check complements the
// platform runtime smoke; it does not claim that an explicit client alone
// fixes every crash in ShowDevTools.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '..', 'src', 'shell_bridge.cc'), 'utf8');
const start = source.indexOf('class ShowDevToolsTask');
const end = source.indexOf('\nvoid PostShowDevTools', start);
assert(start >= 0 && end > start, 'ShowDevToolsTask must be present');
const task = source.slice(start, end);

const clientStart = source.indexOf('class DevToolsBrowserClient');
const clientEnd = source.indexOf('\n};', clientStart);
assert(clientStart >= 0 && clientEnd > clientStart, 'dedicated DevTools client must be defined');
const client = source.slice(clientStart, clientEnd);
assert.ok(client.includes('class DevToolsBrowserClient final : public CefClient'),
  'DevTools must use a client dedicated to its browser, not the source tab client');
assert.ok(client.includes('IMPLEMENT_REFCOUNTING(DevToolsBrowserClient)'),
  'the dedicated CEF client must have valid CEF ref-counting');
assert.match(task, /CefRefPtr<CefClient>\s+devtools_client\s*=\s*new DevToolsBrowserClient\(\)/,
  'the task must create and strongly hold an explicit CEF client');
assert.match(task, /host_->ShowDevTools\(\s*window_info,\s*devtools_client,\s*settings/,
  'ShowDevTools must receive the explicit managed client');
assert.doesNotMatch(task, /host_->ShowDevTools\(\s*window_info,\s*nullptr/,
  'a null client must not make the CEF DevTools browser unmanaged');

console.log('DevTools CEF client regression test passed');
