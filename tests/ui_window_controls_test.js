'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const readText = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const html = readText('resources/ui/index.html');
const host = readText('resources/ui/host-bridge.js');
const native = readText('src/shell_bridge.cc');
const policy = html.match(/<meta http-equiv="Content-Security-Policy" content="([^"]+)"/);
assert(policy, 'the UI Content Security Policy must exist');
const inlineScripts = [...html.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script\s*>/gi)]
  .filter(([, attributes]) => !/\bsrc\s*=/.test(attributes));
assert.equal(inlineScripts.length, 5, 'the number of allowed inline scripts remains stable');
for (const [, , source] of inlineScripts) {
  const hash = crypto.createHash('sha256').update(source).digest('base64');
  assert(policy[1].includes(`'sha256-${hash}'`), 'each inline script must match its CSP hash');
}

const controls = html.match(/<div class="wctl win-ctls" id="winCtl"[\s\S]*?<\/div>/);
assert(controls, 'the Windows window-control group must exist');
for (const [action, label, cls] of [
  ['minimize', 'Свернуть окно', 'wc wc-min'],
  ['maximize', 'Развернуть окно', 'wc wc-max'],
  ['close', 'Закрыть окно', 'wc wc-close'],
]) {
  assert(controls[0].includes(`data-w="${action}"`) &&
      controls[0].includes(`aria-label="${label}"`) &&
      controls[0].includes(`class="${cls}"`),
    `${action} remains the same accessible Windows action`);
}
assert.match(controls[0], /aria-pressed="false"/, 'the maximize button exposes its toggle state');
assert.match(controls[0], /<use href="#i-minus"\/>/);
assert.match(controls[0], /<use href="#i-x"\/>/);
assert.match(controls[0], /class="i win-maximize-icon"/);
assert.match(controls[0], /class="i win-restore-icon"/);
assert(!controls[0].includes('${ico('), 'the window controls must not render a literal template expression');

// Commit 5bba45c keeps Windows controls at the far right of the first UI row:
// toolbar actions normally, and the top tab row when tabs are positioned there.
const appStart = html.indexOf('<div class="app" id="app">');
const toolbarStart = html.indexOf('<header class="tb">');
const toolbarEnd = html.indexOf('</header>', toolbarStart);
const actionsStart = html.indexOf('<div class="tb-g tb-actions"', toolbarStart);
const controlsStart = html.indexOf('<div class="wctl win-ctls" id="winCtl"');
const moreStart = html.indexOf('id="moreBtn"', actionsStart);
assert(appStart >= 0 && toolbarStart > appStart && actionsStart > toolbarStart &&
    controlsStart > moreStart && controlsStart < toolbarEnd,
  '#winCtl starts at the end of the original toolbar action row');
assert(!html.includes('id="winTitle"') && !html.includes('class="win-title"'),
  'no separate titlebar changes the original Windows control position');
assert(!html.includes("setCls('has-win-title'") && !html.includes("setCls('win-title-overlay'"),
  'no separate titlebar behavior is introduced');
const applyTabPosStart = html.indexOf('function applyTabPos() {');
const applyTabPosEnd = html.indexOf('\nfunction ensureTabPos()', applyTabPosStart);
assert(applyTabPosStart >= 0 && applyTabPosEnd > applyTabPosStart);
const applyTabPos = html.slice(applyTabPosStart, applyTabPosEnd);
assert.match(applyTabPos, /if \(pos === 'top'\)[\s\S]*tm\.appendChild\(ctl\)/,
  'top-positioned tabs own the first-row window controls');
assert.match(applyTabPos, /else\s*\{[\s\S]*?home\.appendChild\(ctl\)/,
  'all other tab positions return the controls to the toolbar actions');

// Keep the native window actions and reflect actual maximize/restore state.
const initStart = html.indexOf('function initWinLights() {');
const initEnd = html.indexOf('\nfunction toggleSidebar()', initStart);
assert(initStart >= 0 && initEnd > initStart, 'the window-control initializer must exist');
const init = html.slice(initStart, initEnd);
assert.match(init, /if \(n\.platform === 'darwin'\) \{[\s\S]*?lights\.classList\.add\('live'\)/,
  'the existing macOS traffic-light controls remain intact');
assert.match(init, /if \(n\.platform !== 'win32'\) \{ ctl\.hidden = true; return; \}/,
  'glass window controls are enabled only on Windows');
assert.match(init, /ctl\.addEventListener\('click'/);
for (const action of ['close', 'minimize']) {
  assert(init.includes(`b.dataset.w === '${action}'`) && init.includes(`n.${action}()`),
    `${action} continues to call the native window action`);
}
assert.match(init, /else toggleMaximize\(\)/, 'maximize continues to toggle the native window state');
assert.match(init, /n\.getWindowState\(\)/, 'the actual native maximize state drives Restore semantics');
assert.match(init, /window\.addEventListener\('resize', syncWindowState/);
assert.match(init, /window\.addEventListener\('focus', syncWindowState/);
assert.match(html, /const label = maximized \? 'Восстановить окно' : 'Развернуть окно'/);

const css = [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)].map(match => match[1]).join('\n');
assert.match(css, /--toolbar-gap:8px;/);
assert.match(css, /--window-control-size:32px;/);
assert.match(css, /--window-control-icon-size:14px;/);
assert.match(css, /--window-control-gap:var\(--toolbar-gap\)/);
assert.match(css, /\.wctl\{[^}]*gap:(?:var\(--window-control-gap\)|8px)[^}]*margin-left:10px/,
  'the group keeps its historical placement and spacing in the toolbar');
assert.match(css, /\.tab-mount>\.wctl\{margin-left:auto\}/,
  'the controls align at the far right when tab mounts own the first row');
assert.match(css, /#winCtl\.win-ctls \.wc\{[\s\S]*?width:var\(--window-control-size\);height:var\(--window-control-size\)/);
assert.match(css, /#winCtl\.win-ctls \.wc\{[\s\S]*?border-radius:[^;]+;[\s\S]*?background:linear-gradient\([\s\S]*?var\(--glass-1\)/,
  'the buttons use the existing glass material, not traffic-light fills');
assert.match(css, /#winCtl\.win-ctls \.wc\{[\s\S]*?-webkit-backdrop-filter:var\(--bf-glass\);backdrop-filter:var\(--bf-glass\)/);
assert.match(css, /#winCtl\.win-ctls \.wc:hover\{/);
assert.match(css, /#winCtl\.win-ctls \.wc-close:hover\{[^}]*color:rgb\(var\(--danger\)\)/);
assert.match(css, /#winCtl\.win-ctls \.wc-max\[aria-pressed="true"\] \.win-restore-icon\{display:block\}/);
assert.match(css, /#winCtl\.win-ctls \.wc:active\{/);
assert.match(css, /#winCtl\.win-ctls \.wc:focus-visible\{/);
assert.doesNotMatch(css, /\.wctl \.wc-(?:min|max|close)\{background:#(?:FEBC2E|28C840|FF5F57)\}/,
  'Windows keys do not use macOS traffic-light colors');
assert.doesNotMatch(css, /--wc-(?:top|right|w|reserve)\b/);
assert.doesNotMatch(css, /\.wctl\{[^}]*position:fixed/);
assert.doesNotMatch(css, /:has\(#winCtl:not\(\[hidden\]\)\)\[data-tabpos=/);
assert.doesNotMatch(html, /classList\.(?:add|toggle)\(['"]host-win32['"]|className\s*=\s*['"][^'"]*host-win32/,
  'index.html does not force-enable a global Windows toolbar class');

assert.match(host, /getWindowState: function \(\) \{ return q\('win\.state'\); \}/);
assert.match(native, /if \(m == "win.state"\) \{[\s\S]*?window_->IsMaximized\(\)[\s\S]*?\\"maximized\\":true/,
  'the native bridge returns the actual maximize state for Restore semantics');
const bridgeStart = native.indexOf('bool Shell::HandleBridge(');
const bridgeEnd = native.indexOf('\nvoid Shell::TabAction(', bridgeStart);
assert(bridgeStart >= 0 && bridgeEnd > bridgeStart);
const handler = native.slice(bridgeStart, bridgeEnd);
assert(handler.indexOf('if (!IsUiBrowser(browser))') < handler.indexOf('if (m == "win.state")'),
  'the read-only native window-state query remains behind the central UI authorization boundary');

console.log('Windows window-control placement, glass styling, and native action tests passed');
