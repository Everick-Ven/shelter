'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'resources/ui/index.html'), 'utf8');
const host = fs.readFileSync(path.join(root, 'resources/ui/host-bridge.js'), 'utf8');
const native = fs.readFileSync(path.join(root, 'src/shell_bridge.cc'), 'utf8');
const acceptance = fs.readFileSync(path.join(root, 'tools/acceptance_smoke.py'), 'utf8');

const controls = html.match(/<div class="wctl win-ctls" id="winCtl"[\s\S]*?<\/div>/);
assert(controls, 'the dedicated Windows window-control group must exist');
for (const [action, label] of [['minimize', 'Свернуть окно'], ['maximize', 'Развернуть окно'], ['close', 'Закрыть окно']]) {
  assert(controls[0].includes(`data-w="${action}"`) && controls[0].includes(`aria-label="${label}"`),
    `${action} must be an accessible native window action`);
}
assert.match(controls[0], /aria-pressed="false"/, 'the maximize button exposes its toggle state');
assert.match(html, /if \(n\.platform !== 'win32'\) \{ ctl\.hidden = true; return; \}/,
  'the replacement controls are shown only on Windows');
assert.match(html, /if \(n\.platform === 'darwin'\) \{[\s\S]*?lights\.classList\.add\('live'\)/,
  'the existing macOS traffic-light controls remain intact');
assert.match(html, /if \(pos === 'top'\)[\s\S]*?tm\.appendChild\(ctl\)[\s\S]*?else[\s\S]*?home\.appendChild\(ctl\)/,
  'Windows controls move with the tab row to the first row for all tab positions');

const css = [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)].map(match => match[1]).join('\n');
assert.match(css, /#winCtl\.win-ctls \.wc\{[\s\S]*?border-radius:[^;]+;[\s\S]*?background:[^;]+;[\s\S]*?box-shadow:var\(--glass-shadow-md\)/,
  'Windows keys use the glass control material');
assert.match(css, /#winCtl\.win-ctls \.wc:active\{/);
assert.match(css, /#winCtl\.win-ctls \.wc:focus-visible\{/);
assert.match(css, /\.host-win32 \.tb>\.omni-tb\{width:min\(620px,28vw\)\}/,
  'the centered address field yields space for Windows controls on narrow desktop widths');
assert.match(css, /-webkit-app-region:no-drag/);
assert.doesNotMatch(css, /\.wctl \.wc-(?:min|max|close)\{background:#(?:FEBC2E|28C840|FF5F57)\}/,
  'the Windows controls no longer use Mac traffic-light colors');

assert.match(host, /getWindowState: function \(\) \{ return q\('win\.state'\); \}/);
assert.match(native, /if \(m == "win\.state"\) \{[\s\S]*?window_->IsMaximized\(\)[\s\S]*?\\"maximized\\":true/,
  'the native bridge returns the actual maximize state for Restore semantics');
const bridgeStart = native.indexOf('bool Shell::HandleBridge(');
const bridgeEnd = native.indexOf('\nvoid Shell::TabAction(', bridgeStart);
assert(bridgeStart >= 0 && bridgeEnd > bridgeStart);
const handler = native.slice(bridgeStart, bridgeEnd);
assert(handler.indexOf('if (!IsUiBrowser(browser))') < handler.indexOf('if (m == "win.state")'),
  'the read-only native window-state query remains behind the central UI authorization boundary');

assert.match(acceptance, /fireActionTile: controls\.children\.length === 2[\s\S]*?pomodoroGlobalIndicator: controls\.children\.length === 2/,
  'runtime acceptance must validate both the session-clear control and the new global Pomodoro indicator');

console.log('Windows window-control styling, platform gating, placement, and native state tests passed');
