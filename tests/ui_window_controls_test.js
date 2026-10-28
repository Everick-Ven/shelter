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
assert.match(controls[0], /<use href="#i-minus"\/>/);
assert.match(controls[0], /<use href="#i-x"\/>/);
assert(!controls[0].includes('${ico('), 'the window controls must not render a literal template expression');
assert.match(html, /<symbol id="i-minus"/);
assert.match(html, /<symbol id="i-x"/);
assert.match(html, /const label = maximized \? 'Восстановить окно' : 'Развернуть окно'/,
  'the accessible label follows the actual native maximize state');
assert.match(html, /--window-control-gap:var\(--toolbar-gap\)/);
assert.match(html, /\.tab-mount\{display:flex;align-items:center;gap:var\(--toolbar-gap\)/);
// Keep controls at the same viewport corner, but nest them after the toolbar's
// drag region. CEF applies draggable/no-drag rectangles in order; placing this
// no-drag sibling before .tb lets the later toolbar union cover the buttons.
const toolbarStart = html.indexOf('<header class="tb">');
const actionsStart = html.indexOf('<div class="tb-g tb-actions">', toolbarStart);
const controlsStart = html.indexOf('<div class="wctl win-ctls" id="winCtl"', toolbarStart);
const toolbarEnd = html.indexOf('</header>', toolbarStart);
assert(toolbarStart >= 0 && actionsStart > toolbarStart && controlsStart > actionsStart && controlsStart < toolbarEnd,
  'the no-drag window controls must follow the draggable toolbar in CEF region order');
assert.doesNotMatch(html, /<div class="app" id="app">\s*<!--[^>]*-->\s*<div class="wctl win-ctls" id="winCtl"/,
  'the window controls must not precede and be covered by the later toolbar drag region');
assert.doesNotMatch(html, /tm\.appendChild\(ctl\)|home\.appendChild\(ctl\)/,
  'applyTabPos must not move the window controls between containers');
assert.match(html, /#winCtl\.win-ctls\{position:fixed;top:var\(--wc-top\);right:var\(--wc-right\);/,
  'the window controls use fixed window-corner positioning');
assert.match(html, /\.host-win32 \.app\[data-tabpos="top"\] #tabTop\{padding-right:var\(--wc-reserve\)\}/);
assert.match(html, /\.host-win32 \.app\[data-tabpos="left"\] \.tb,\.host-win32 \.app\[data-tabpos="bottom"\] \.tb\{padding-right:var\(--wc-reserve\)\}/);
assert.match(html, /\.host-win32 \.app\[data-tabpos="right"\] \.tab-rail\{padding-top:/);
assert.match(html, /if \(n\.platform !== 'win32'\) \{ ctl\.hidden = true; return; \}/,
  'the replacement controls are shown only on Windows');
assert.match(html, /if \(n\.platform === 'darwin'\) \{[\s\S]*?lights\.classList\.add\('live'\)/,
  'the existing macOS traffic-light controls remain intact');
const initStart = html.indexOf('function initWinLights() {');
const initEnd = html.indexOf('\nfunction toggleSidebar()', initStart);
assert(initStart >= 0 && initEnd > initStart, 'the native control initializer must exist');
const winInit = html.slice(initStart, initEnd);
assert.match(winInit, /document\.documentElement\.classList\.toggle\('host-win32', n\.platform === 'win32'\)/,
  'Windows must activate the host layout that reserves toolbar space for its fixed window controls');
assert.match(winInit, /ctl\.addEventListener\('click'/, 'the window controls must listen for clicks');
for (const action of ['close', 'minimize']) {
  assert(winInit.includes(`b.dataset.w === '${action}'`) && winInit.includes(`n.${action}()`),
    `${action} must reach its native window action`);
}
assert.match(winInit, /else toggleMaximize\(\)/, 'the maximize button toggles the native state');

const css = [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)].map(match => match[1]).join('\n');
assert.match(css, /--toolbar-gap:8px;/);
assert.match(css, /--window-control-size:32px;/);
assert.match(css, /--window-control-icon-size:14px;/);
assert.match(css, /#winCtl\.win-ctls \.wc\{[\s\S]*?border-radius:[^;]+;[\s\S]*?background:[^;]+;[\s\S]*?box-shadow:var\(--glass-shadow-md\)/,
  'Windows keys use the glass control material');
assert.match(css, /#winCtl\.win-ctls\{position:fixed;[^}]*display:flex;align-items:center;gap:var\(--window-control-gap\);/);
assert.match(css, /#winCtl\.win-ctls \.wc\{[\s\S]*?width:var\(--window-control-size\);height:var\(--window-control-size\)/);
assert.match(css, /#winCtl\.win-ctls \.wc:hover\{/,
  'all three controls provide visible hover feedback');
assert.match(css, /#winCtl\.win-ctls \.wc-close:hover\{[^}]*color:rgb\(var\(--danger\)\)/);
assert.match(css, /#winCtl\.win-ctls \.wc-max\[aria-pressed="true"\] \.win-restore-icon\{display:block\}/);
assert.match(css, /#winCtl\.win-ctls \.wc:active\{/);
assert.match(css, /#winCtl\.win-ctls \.wc:focus-visible\{/);
assert.match(css, /@media\s*\(min-width:1025px\) and \(max-width:1800px\)\{[\s\S]*?\.host-win32 \.tb\{display:flex\}[\s\S]*?\.host-win32 \.tb>\.omni-tb\{flex:1 1 0;width:auto;min-width:140px;max-width:620px;margin:0 auto\}/,
  'the Windows toolbar reserves a flexible address field between its action groups at desktop widths');
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
assert.match(acceptance, /noHorizontalOverflow:[\s\S]*?visuallyOutOfHeader\.length === 0/,
  'runtime acceptance checks visible toolbar bounds rather than counting glass-shadow overflow');

console.log('Windows window-control styling, platform gating, placement, and native state tests passed');
