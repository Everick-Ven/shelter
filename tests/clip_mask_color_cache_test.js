'use strict';

// P0-2: цвет маски окна (Windows) кэшируется между кадрами clipTick/kickClip.
// Приёмка ТЗ: view.clip уходит с тем же maskColor (формула не менялась), а
// число вызовов getImageData за открытие меню падает до 0–3. macOS-ветка
// (USE_HOLES) не тронута. Плюс цепочка инвалидации: refreshMask слушает
// data-theme/style/class на корне и data-gfx на body; через root.style идёт
// смена --glass-op (applyGlassOp) — кэш сбрасывается и там.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf-8');
const bridge = read('resources/ui/host-bridge.js');
const html = read('resources/ui/index.html');

// --- исходники: ветки платформ и формула маски не менялись -------------------
assert.match(bridge, /var USE_HOLES = PLATFORM === 'darwin';/, 'macOS holes branch untouched');
assert.match(bridge, /var USE_WINDOWS_MASK = PLATFORM === 'win32';/, 'windows mask branch kept');
assert.ok(bridge.includes('if (!USE_WINDOWS_MASK) return 0;'), 'clipMaskColor stays Windows-only');
assert.match(bridge, /function clipTick\(\) \{\s*\n\s*if \(!USE_HOLES \|\|/,
  'the rAF clip loop still runs only on the macOS holes branch');

// формула цвета совпадает с прежней — maskColor байт-в-байт тот же
const maskStart = bridge.indexOf('function clipMaskColor()');
const maskEnd = bridge.indexOf('function syncClip()', maskStart);
const maskSrc = bridge.slice(maskStart, maskEnd);
for (const needle of [
  "readCssColor(cs.getPropertyValue('--surface').trim())",
  "readCssColor(cs.getPropertyValue('--glass-surface').trim())",
  "parseFloat(cs.getPropertyValue('--glass-op'))",
  "readCssColor(cs.getPropertyValue('--bg').trim())",
  'var alpha = glass[3] / 255;',
  'Math.round(glass[0] * alpha + base[0] * (1 - alpha))',
  '(r << 16) | (g << 8) | b',
]) {
  assert.ok(maskSrc.includes(needle), `mask formula kept: ${needle}`);
}

// --- инвалидация кэша ------------------------------------------------------
assert.match(bridge, /var clipColorCache = null;\s*\n\s*function invalidateClipMaskColor\(\) \{ clipColorCache = null; \}/,
  'cache has an explicit invalidation hook');
const refreshStart = bridge.indexOf('var refreshMask = function () {');
const refreshEnd = bridge.indexOf('};', refreshStart);
const refreshSrc = bridge.slice(refreshStart, refreshEnd);
assert.match(refreshSrc, /invalidateClipMaskColor\(\);/,
  'refreshMask drops the cached color');
assert.ok(refreshSrc.indexOf('invalidateClipMaskColor()') <
          refreshSrc.indexOf('if (!st.visible || st.frozen || st.busy) return;'),
  'the cache is dropped even when the window is hidden/frozen at the moment of the change');
assert.match(bridge, /attributeFilter: \['data-theme', 'style', 'class'\]/,
  'root theme/style/class still invalidate');
assert.match(bridge, /attributeFilter: \['data-gfx'\]/,
  'body data-gfx still invalidates');
// смена --glass-op идёт через root.style — значит попадает в тот же сброс
assert.match(html, /document\.documentElement\.style\.setProperty\('--glass-op'/,
  'applyGlassOp writes --glass-op via root style (covered by the style observer)');

// --- поведение: реальный clipMaskColor + счётчик getImageData ----------------
const canvasStart = bridge.indexOf('var clipColorCanvas = document.createElement');
assert.ok(canvasStart >= 0, 'clip color probe found');
const probeSrc = bridge.slice(canvasStart, bridge.indexOf('\n  function syncClip', canvasStart));

function makeEnv() {
  const counts = { getImageData: 0, computed: 0 };
  let fill = '';
  const ctx = {
    clearRect() {}, fillRect() {},
    set fillStyle(v) { fill = String(v); },
    get fillStyle() { return fill; },
    getImageData() {
      counts.getImageData++;
      const m = fill.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)(?:,\s*([\d.]+))?/);
      if (!m) return { data: new Uint8ClampedArray([0, 0, 0, 0]) };
      const a = m[4] === undefined ? 255 : Math.round(parseFloat(m[4]) * 255);
      return { data: new Uint8ClampedArray([+m[1], +m[2], +m[3], a]) };
    },
  };
  const sandbox = {
    USE_WINDOWS_MASK: true,
    root: {},
    byId: () => null,
    getComputedStyle: () => {
      counts.computed++;
      return { getPropertyValue: (name) => ({ '--surface': 'rgb(20,20,20)', '--glass-surface': 'rgba(0,0,0,0)', '--glass-op': '82', '--bg': 'rgb(0,0,0)' }[name] || '') };
    },
    document: { createElement: () => ({ getContext: () => ctx, width: 0, height: 0 }) },
  };
  vm.runInNewContext(probeSrc + '\nthis.__probe = { clipMaskColor, invalidateClipMaskColor };', sandbox);
  return { counts, probe: sandbox.__probe };
}

{
  const { counts, probe } = makeEnv();
  const first = probe.clipMaskColor();
  // прозрачная стеклянная поверхность -> смесь surface по --glass-op: 82%:
  // 209/255 альфы, r=g=b=round(20*209/255)=16 — цвет маски 0x101010.
  assert.equal(first, (16 << 16) | (16 << 8) | 16, 'mask color matches the documented formula');
  assert.equal(counts.getImageData, 3, 'fresh compute reads exactly three css colors');
  for (let i = 0; i < 40; i++) {
    const v = probe.clipMaskColor();
    assert.equal(v, first, 'cached color is stable across frames');
  }
  assert.equal(counts.getImageData, 3, 'per-frame reads drop to zero while the theme holds');
  assert.equal(counts.computed, 1, 'getComputedStyle is not repeated per frame');
  probe.invalidateClipMaskColor();
  const again = probe.clipMaskColor();
  assert.equal(again, first, 'invalidation recomputes the same maskColor');
  assert.equal(counts.getImageData, 6, 'one menu open with one theme change costs at most 2x3 reads');
  console.log('getImageData over 40 frames + 1 invalidation:', counts.getImageData, '(old behaviour: 123)');
}

console.log('clip mask color cache regression passed');
