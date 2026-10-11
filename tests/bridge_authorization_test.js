'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const bridge = fs.readFileSync(path.join(root, 'src', 'shell_bridge.cc'), 'utf8');
const start = bridge.indexOf('bool Shell::HandleBridge(');
const end = bridge.indexOf('\nvoid Shell::TabAction(', start);
assert(start >= 0 && end > start, 'Shell::HandleBridge must be present');
const handler = bridge.slice(start, end);
assert.match(handler, /CEF_REQUIRE_UI_THREAD\(\);\s*\/\/[\s\S]*?if \(!IsUiBrowser\(browser\)\)/);
assert.strictEqual((handler.match(/IsUiBrowser\(/g) || []).length, 1,
  'all bridge methods must rely on the single entry authorization check');
for (const method of ['clip.read', 'clip.write', 'ext.install']) {
  assert(handler.includes(`m == "${method}"`), `${method} must remain handled by the authorized bridge`);
}

const clients = fs.readFileSync(path.join(root, 'src', 'clients.cc'), 'utf8');
assert.match(clients, /IsUiBrowser\(browser\)[\s\S]{0,160}IsUiUrl\(frame->GetURL\(\)\.ToString\(\)\)/,
  'the message-router boundary must also reject non-UI frames before legacy routing');
console.log('central UI-browser bridge authorization test passed');
