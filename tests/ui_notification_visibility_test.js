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
  '#dlFloat:not([hidden])',
  '.pomo-popover:not([hidden])',
  '.pomo-notice:not([hidden])'
]) {
  assert.ok(holes.has(selector), `Слой ${selector} не защищён от перекрытия нативной страницей`);
}

const styleMatch = html.match(/<style[^>]*>([\s\S]*?)<\/style>/i);
assert.ok(styleMatch, 'Не найдена таблица стилей уведомлений');
const css = styleMatch[1].replace(/\/\*[\s\S]*?\*\//g, '');
const toastRootRule = css.match(/\.toast-root\{([^}]*)\}/);
assert.ok(toastRootRule, 'Не найден верхнеуровневый CSS-слой уведомлений');
assert.match(toastRootRule[1], /position:fixed/,
  'уведомления должны находиться вне обрезающего контейнера рабочей области');
const toastZ = Number((toastRootRule[1].match(/z-index:(\d+)/) || [])[1]);
const pinLockRule = css.match(/\.app-pin-lock\{([^}]*)\}/);
assert.ok(pinLockRule, 'Не найден верхнеуровневый слой PIN-блокировки');
const pinLockZ = Number((pinLockRule[1].match(/z-index:(\d+)/) || [])[1]);
assert.ok(pinLockZ > toastZ, 'экран PIN-блокировки должен перекрывать уведомления');
const otherZ = Array.from(css.replace(toastRootRule[0], '').replace(pinLockRule[0], '').matchAll(/z-index\s*:\s*(\d+)/g), match => Number(match[1]));
assert.ok(toastZ > Math.max(...otherZ), 'уведомления должны быть выше обычных UI-попапов, но ниже экрана PIN-блокировки');

const toastMarkupAt = html.indexOf('<div class="toast-root" id="toasts"');
const appMarkupAt = html.indexOf('<div class="app" id="app">');
assert.ok(toastMarkupAt > appMarkupAt, 'слой уведомлений должен находиться после каркаса приложения');
const divMarkupBeforeToast = html.slice(appMarkupAt, toastMarkupAt).replace(/<!--[\s\S]*?-->/g, '');
let divDepth = 0;
for (const tag of divMarkupBeforeToast.matchAll(/<\/?div\b[^>]*>/g)) divDepth += tag[0][1] === '/' ? -1 : 1;
assert.equal(divDepth, 0, 'слой уведомлений должен быть соседом #app, а не его потомком');
for (const id of ['floatStack', 'noteFloat', 'dlFloat']) {
  assert.ok(html.indexOf(`id="${id}"`) > toastMarkupAt, `Плавающий слой ${id} должен оставаться внутри портала уведомлений`);
}

const positionStart = html.indexOf('function positionToastRoot() {');
const positionEnd = html.indexOf('const debounce =', positionStart);
assert.ok(positionStart >= 0 && positionEnd > positionStart, 'Не найдена привязка портала к рабочей области');
const positionCode = html.slice(positionStart, positionEnd);
assert.match(positionCode, /viewport\.getBoundingClientRect\(\)/);
assert.match(positionCode, /rect\.left/);
assert.match(positionCode, /window\.innerHeight - rect\.bottom/);
assert.match(positionCode, /new ResizeObserver\(positionToastRoot\)/);
assert.match(positionCode, /addEventListener\('resize', positionToastRoot/);
assert.match(positionCode, /addEventListener\('transitionend', positionToastRoot/);
const observed = [];
const viewportListeners = new Map();
const windowListeners = new Map();
const toastStyle = {};
const viewport = {
  rect: { left: 100, bottom: 700, width: 650 },
  getBoundingClientRect() { return this.rect; },
  addEventListener(name, listener) { viewportListeners.set(name, listener); }
};
const toastRoot = { style: toastStyle };
class FakeResizeObserver {
  constructor(listener) { this.listener = listener; }
  observe(node) { observed.push({ node, listener: this.listener }); }
}
const fakeWindow = {
  innerHeight: 800,
  ResizeObserver: FakeResizeObserver,
  addEventListener(name, listener) { windowListeners.set(name, listener); }
};
const positionToastRoot = new Function('$', 'window', 'ResizeObserver',
  positionCode + '\nreturn positionToastRoot;')(selector => ({ '#toasts': toastRoot, '#viewport': viewport }[selector]), fakeWindow, FakeResizeObserver);
assert.equal(toastStyle.left, '105px');
assert.equal(toastStyle.bottom, '105px');
assert.equal(toastStyle.maxWidth, '640px');
viewport.rect = { left: 210, bottom: 610, width: 420 };
positionToastRoot();
assert.equal(toastStyle.left, '215px');
assert.equal(toastStyle.bottom, '195px');
assert.equal(toastStyle.maxWidth, '410px');
assert.ok(observed.some(entry => entry.node === viewport), 'размер рабочей области должен отслеживаться');
assert.ok(windowListeners.has('resize') && viewportListeners.has('transitionend'),
  'привязка уведомлений должна обновляться при изменении геометрии окна');

const exportStart = html.indexOf('function wpOpenTaskExportDialog() {');
const exportEnd = html.indexOf('\nfunction wpExportTasks()', exportStart);
assert.ok(exportStart >= 0 && exportEnd > exportStart, 'Не найден диалог экспорта задач');
const exportDialog = html.slice(exportStart, exportEnd);
assert.match(exportDialog, /openModal\(/);
assert.match(exportDialog, /toast\('Код файла должен содержать не менее 8 символов\.'/,
  'ошибка короткого кода должна показываться через общий слой уведомлений');
assert.match(exportDialog, /toast\('Коды не совпадают\.'/,
  'ошибка несовпадения кодов должна показываться через общий слой уведомлений');
assert.match(exportDialog, /toast\('Не удалось зашифровать экспорт\. Файл не создан\.'/,
  'ошибка шифрования экспорта должна оставаться обычным toast-уведомлением');

assert.match(
  bridge,
  /var OVERLAY_SEL = USE_HOLES \? FREEZE_BASE : FREEZE_BASE \+ ',' \+ HOLE_SEL;/,
  'На Windows активное уведомление должно запускать снимок, на macOS — вырезаться из нативного слоя'
);
assert.ok(!bridge.includes('sh-snap-wait'), 'получение снимка не должно скрывать интерфейс на время ожидания');
assert.match(bridge, /var POPUP_ROOTS = '[^']*\.pomo-popover[^']*\.pomo-notice/,
  'Pomodoro-панели должны отслеживаться для обновления нативных отверстий');
assert.match(bridge, /var HOLE_ROOTS = '[^']*\.pomo-popover[^']*\.pomo-notice/,
  'Pomodoro-панели должны учитываться при обновлении отверстий в нативной странице');
const freezeStart = bridge.indexOf('  function freeze() {');
const freezeEnd = bridge.indexOf('  function thaw()', freezeStart);
assert.ok(freezeStart >= 0 && freezeEnd > freezeStart, 'Не найден цикл замены нативного вида снимком');
const freezeCode = bridge.slice(freezeStart, freezeEnd);
const noSnapshotPath = freezeCode.indexOf('if (!url) { captureFailed = true; dropSnap(); return false; }');
const hideNativeView = freezeCode.indexOf('hideSent = true;');
assert.ok(noSnapshotPath >= 0 && hideNativeView > noSnapshotPath, 'при неудаче снимка нативный вид остаётся показанным');
assert.ok(freezeCode.includes('waitForSnapshot(img, 500)'), 'снимок декодируется до скрытия нативной страницы');
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
assert.match(nativePopupStyle, /\.dl-float,/);
assert.match(nativePopupStyle, /\.pomo-popover,/);
assert.match(nativePopupStyle, /\.pomo-notice\{/);
assert.match(nativePopupStyle, /background-color:var\(--native-popup-menu\)/);
assert.match(nativePopupStyle, /background-image:linear-gradient\(var\(--native-contrast-veil\)/);

const snapshotReadyStart = bridge.indexOf('  function waitForSnapshot(img, timeout) {');
const snapshotReadyEnd = bridge.indexOf('\n  function dropSnap()', snapshotReadyStart);
assert.ok(snapshotReadyStart >= 0 && snapshotReadyEnd > snapshotReadyStart, 'Не найдено ожидание готовности изображения снимка');
const snapshotReadyCode = bridge.slice(snapshotReadyStart, snapshotReadyEnd);
const waitForSnapshot = new Function(snapshotReadyCode + '\nreturn waitForSnapshot;')();

(async function testSnapshotReadiness() {
  const image = { complete: true, naturalWidth: 2, decode: () => Promise.resolve() };
  assert.strictEqual(await waitForSnapshot(image, 50), true, 'готовый декодированный снимок можно безопасно показать');
  const broken = { complete: true, naturalWidth: 0, decode: () => Promise.reject(new Error('decode failed')) };
  assert.strictEqual(await waitForSnapshot(broken, 50), false, 'повреждённый снимок не должен скрывать нативную страницу');
  assert.strictEqual(await waitForSnapshot(null, 50), false, 'отсутствующий снимок не считается готовым');
  console.log('Notification visibility and native-view snapshot recovery tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
