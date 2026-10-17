'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const bridge = fs.readFileSync(path.join(root, 'resources/ui/host-bridge.js'), 'utf8');
const html = fs.readFileSync(path.join(root, 'resources/ui/index.html'), 'utf8');

const holeMatch = bridge.match(/var HOLE_SEL = '([^']+)';/);
assert.ok(holeMatch, 'Не найден список слоёв, которым нужно оставаться над нативной страницей');
const holes = new Set(holeMatch[1].split(','));
for (const selector of [
  '.tip',
  '#toasts>.toast',
  '.suggest:not([hidden])',
  '#noteFloat:not([hidden])',
  '#dlFloat:not([hidden])'
]) {
  assert.ok(holes.has(selector), `Слой ${selector} не защищён от перекрытия нативной страницей`);
}

assert.match(
  bridge,
  /var OVERLAY_SEL = USE_HOLES \? FREEZE_BASE : FREEZE_BASE \+ ',' \+ HOLE_SEL;/,
  'На Windows активное уведомление должно запускать снимок, на macOS — вырезаться из нативного слоя'
);
assert.match(
  bridge,
  /function holeRects\(vp\) \{[\s\S]*?document\.querySelectorAll\(HOLE_SEL\)/,
  'macOS должен строить отверстия для всех видимых уведомлений'
);
assert.match(
  bridge,
  /watchOverlayRoot\(byId\('toasts'\), true\)/,
  'появление и скрытие уведомления должно обновлять снимок или отверстие'
);
assert.match(
  bridge,
  /attributeFilter: \['class', 'hidden'\]/,
  'наблюдатель должен замечать показ и скрытие уведомлений'
);
assert.match(
  html,
  /<div class="dl-float" id="noteFloat" hidden aria-live="polite">/,
  'в интерфейсе должен существовать отдельный слой обычных уведомлений'
);
assert.match(
  html,
  /function noteShow\(title, body, ms\)[\s\S]*?f\.hidden = false;/,
  'уведомление должно становиться видимым при показе'
);

const nativePopupStart = html.indexOf('body.native-content-visible:not([data-gfx="perf"]) .menu');
const nativePopupEnd = html.indexOf('body.native-content-visible:not([data-gfx="perf"]) .modal', nativePopupStart);
assert.ok(nativePopupStart >= 0 && nativePopupEnd > nativePopupStart, 'Не найдены стили поверх нативной страницы');
const nativePopupStyle = html.slice(nativePopupStart, nativePopupEnd);
assert.match(nativePopupStyle, /\.dl-float\{/);
assert.match(nativePopupStyle, /background-color:var\(--native-popup-menu\)/);
assert.match(nativePopupStyle, /background-image:linear-gradient\(var\(--native-contrast-veil\)/);

console.log('Notification visibility over native pages and blurred backgrounds passed');
