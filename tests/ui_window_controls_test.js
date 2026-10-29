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
// The Windows titlebar owns #winCtl outside .app. This keeps the drag region
// out of all toolbar transforms/filters and gives the controls one stable row.
const titlebarComment = html.indexOf('<!-- ============ ВЕРХНЯЯ СТРОКА ОКНА (Windows)');
const titlebarStart = html.indexOf('<div class="win-title" id="winTitle"', titlebarComment);
const controlsStart = html.indexOf('<div class="wctl win-ctls" id="winCtl"');
const appStart = html.indexOf('<div class="app" id="app">');
const toolbarStart = html.indexOf('<header class="tb">');
const actionsStart = html.indexOf('<div class="tb-g tb-actions">', toolbarStart);
const toolbarEnd = html.indexOf('</header>', toolbarStart);
assert(titlebarComment >= 0 && titlebarStart > titlebarComment &&
    controlsStart > titlebarStart && controlsStart < appStart && appStart > 0,
  'the titlebar and window controls must be before and outside .app');
assert(toolbarStart > appStart && actionsStart > toolbarStart && toolbarEnd > actionsStart,
  'the regular toolbar remains inside .app below the titlebar');
assert(!html.slice(toolbarStart, toolbarEnd).includes('id="winCtl"'),
  'window controls must not be inside the Visual toolbar backdrop-filter containing block');
assert.doesNotMatch(html, /<!-- Keep fixed controls outside the blurred toolbar/,
  'the old fixed-control placement comment is removed');
assert.doesNotMatch(html, /tm\.appendChild\(ctl\)|home\.appendChild\(ctl\)/,
  'applyTabPos must not move the window controls between containers');
assert.doesNotMatch(html, /Кнопки окна Windows \(#winCtl\) не переносятся: они position:fixed/,
  'the stale fixed-position explanation is removed from applyTabPos');
assert.match(html, /if \(n\.platform !== 'win32'\) \{ ctl\.hidden = true; return; \}/,
  'the existing window controls are shown only on Windows');
assert.doesNotMatch(html, /classList\.(?:add|toggle)\(['"]host-win32['"]|className\s*=\s*['"][^'"]*host-win32/,
  'index.html does not force-enable the global Windows toolbar class');
assert.match(html, /if \(n\.platform === 'darwin'\) \{[\s\S]*?lights\.classList\.add\('live'\)/,
  'the existing macOS traffic-light controls remain intact');
const initStart = html.indexOf('function initWinLights() {');
const initEnd = html.indexOf('\nfunction toggleSidebar()', initStart);
assert(initStart >= 0 && initEnd > initStart, 'the native control initializer must exist');
const winInit = html.slice(initStart, initEnd);
assert.match(winInit, /ctl\.addEventListener\('click'/, 'the window controls must listen for clicks');
for (const action of ['close', 'minimize']) {
  assert(winInit.includes(`b.dataset.w === '${action}'`) && winInit.includes(`n.${action}()`),
    `${action} must reach its native window action`);
}
assert.match(winInit, /else toggleMaximize\(\)/, 'the maximize button toggles the native state');
assert.match(winInit, /n\.getWindowState\(\)/,
  'the native maximize state is queried from the platform bridge');
assert.match(winInit, /window\.addEventListener\('resize', syncWindowState/);
assert.match(winInit, /window\.addEventListener\('focus', syncWindowState/);

const titlebarScriptStart = html.lastIndexOf('<script>');
const titlebarScriptClose = html.lastIndexOf('</script>');
const bodyClose = html.lastIndexOf('</body>');
assert(titlebarScriptStart >= 0 && titlebarScriptClose > titlebarScriptStart && titlebarScriptClose < bodyClose,
  'the Windows titlebar controller is the final inline script');
assert.strictEqual(html.slice(titlebarScriptClose + '</script>'.length, bodyClose).trim(), '',
  'the titlebar controller is immediately before body close');
const titlebarScript = html.slice(titlebarScriptStart + '<script>'.length, titlebarScriptClose);
assert(titlebarScript.includes("if (native && platform !== 'win32')") &&
    titlebarScript.includes('bar.parentNode.removeChild(bar)'),
  'the titlebar is removed from the DOM on macOS and other native platforms');
assert(titlebarScript.indexOf('if (ctl.dataset.controlsBound) return;') >= 0 &&
    titlebarScript.indexOf('if (ctl.dataset.controlsBound) return;') <
      titlebarScript.indexOf('if (ctl.dataset.titlebarBound) return;'),
  'the titlebar controller never double-binds the native button handlers');
assert(titlebarScript.includes('var HIDE_DELAY = 320'));
assert(titlebarScript.includes('if (y <= HOT) { reveal(); return; }'));
assert(titlebarScript.includes("if (bar.matches(':hover')) return;"),
  'the hidden timer does not dismiss the bar while the pointer is over it');
assert(titlebarScript.includes("a.matches(':focus-visible')"),
  'only keyboard-visible focus keeps the titlebar open');
assert(titlebarScript.includes('obs.disconnect();') && titlebarScript.includes('watch();'),
  'self-authored body classes are kept out of the mutation observer loop');
assert(titlebarScript.includes("bar.addEventListener('dblclick'"),
  'double-clicking the titlebar toggles the window without binding button double-clicks');
const css = [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)].map(match => match[1]).join('\n');
assert.match(css, /--toolbar-gap:8px;/);
assert.match(css, /--window-control-size:32px;/);
assert.match(css, /--window-control-icon-size:14px;/);
assert.match(css, /--titlebar-h:calc\(var\(--window-control-size\) \+ 10px\);/,
  'the separate titlebar is 42px high with the current control size');
assert.match(css, /--titlebar-pad:10px;/);
assert.match(css, /\.win-title\{\s*position:fixed;top:0;left:0;right:0;height:var\(--titlebar-h\);z-index:60;/,
  'the Windows titlebar is fixed across the full window width');
assert.match(css, /\.win-title\{[^}]*-webkit-app-region:drag/,
  'the titlebar remains a CEF drag region');
assert.match(css, /#winCtl\.win-ctls\{position:static;display:flex;align-items:center;gap:var\(--window-control-gap\);flex:none;margin:0;padding:0;-webkit-app-region:no-drag\}/,
  'the controls are no-drag children of the independent titlebar');
assert.match(css, /body\.has-win-title \.win-title\{display:flex\}/);
assert.match(css, /body\.has-win-title:not\(\.win-title-overlay\) \.app\{padding-top:var\(--titlebar-h\)\}/,
  'the app begins below the titlebar only in ordinary window mode');
assert.match(css, /body\.win-title-overlay \.win-title\{transform:translateY\(-100%\);opacity:0;pointer-events:none\}/);
assert.match(css, /body\.win-title-overlay\.win-title-shown \.win-title\{/,
  'the overlay titlebar can reappear over fullscreen content');
assert.match(css, /body\.has-win-title \.sb-top\{display:none\}/,
  'the Windows sidebar header is removed when the separate titlebar is present');
assert.match(css, /\.app\{--wc-edge:10px\}/,
  'the app no longer reserves a corner for fixed window controls');
assert.doesNotMatch(css, /--wc-(?:top|right|w|reserve)\b/,
  'the old fixed-position and toolbar-reservation variables are removed');
assert.doesNotMatch(css, /:has\(#winCtl:not\(\[hidden\]\)\)\[data-tabpos=/,
  'tab-position-specific window-control reservations are removed');
assert.doesNotMatch(css, /#winCtl\.win-ctls\{[^}]*position:fixed/,
  'the control group is no longer fixed to the viewport corner');
assert.match(css, /#winCtl\.win-ctls \.wc\{[\s\S]*?border-radius:[^;]+;[\s\S]*?background:[^;]+;[\s\S]*?box-shadow:var\(--glass-shadow-md\)/,
  'Windows keys use the existing glass control material');
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
assert.match(acceptance, /windowControlsStableAcrossTabPositionsAndGfxModes:[\s\S]*?windowControlsGeometry/,
  'runtime acceptance verifies Windows titlebar controls stay put across tab positions and graphics modes');
assert.match(acceptance, /windowControlsDoNotOverlapToolbar:/,
  'runtime acceptance checks that window controls do not cover toolbar actions');
assert.match(acceptance, /sidebarCollapsedControlsStable:/,
  'runtime acceptance checks the titlebar controls remain fixed when the sidebar collapses');
assert.match(acceptance, /def window_titlebar_probe\(/,
  'runtime acceptance exercises normal and overlay titlebar states');
assert.match(acceptance, /SHELTER_ACCEPTANCE_WINDOW_TITLEBAR_PASS/,
  'CI reports the Windows titlebar behavior probe');
assert.match(acceptance, /ui\.call\([\"']Input\.dispatchMouseEvent[\"']/,
  'runtime acceptance uses CDP pointer movement to reveal and leave the titlebar');
assert.match(acceptance, /setTimeout\(resolve, 450\)[\s\S]*?const afterDelay = [\s\S]*?setTimeout\(resolve, 300\)/,
  'runtime acceptance checks the 320ms hide timer before waiting for the CSS transition');

console.log('Windows window-control styling, platform gating, placement, and native state tests passed');
