'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const bridge = fs.readFileSync(path.join(root, 'resources/ui/host-bridge.js'), 'utf8');
const native = fs.readFileSync(path.join(root, 'src/shell_bridge.cc'), 'utf8');

async function main() {
const extract = (source, startMarker, endMarker, label) => {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start);
  assert.ok(start >= 0 && end > start, `${label} must exist`);
  return source.slice(start, end);
};
const overlaySource = extract(bridge, '  function overlayOverViewport() {', '  function showSnap(', 'overlay intersection check');
const evaluateSource = extract(bridge, '  function evaluate() {', '  var evalQueued', 'visibility evaluation');
const retrySource = extract(bridge, '  function scheduleFreezeRetry() {', '  function evaluate()', 'freeze retry scheduler');
const freezeSource = extract(bridge, '  function freeze() {', '  function thaw()', 'freeze state machine');
const thawSource = extract(bridge, '  function thaw() {', '  /* ---------- скругление углов', 'thaw state machine');

assert.match(freezeSource, /if \(st\.busy \|\| st\.frozen \|\| !st\.visible \|\| !st\.tabId\) return;/,
  'freeze cannot start unless a site tab is visible');
assert.match(freezeSource, /if \(!st\.visible\) \{ finish\(\); return; \}/,
  'a stale freeze recovery must not send a second native hide after the UI is hidden');
assert.match(thawSource, /if \(st\.busy \|\| !st\.frozen \|\| !st\.visible\) return;/,
  'thaw cannot start unless a frozen site tab is visible');
assert.match(thawSource, /if \(!st\.visible\) \{\s*dropSnap\(\);\s*finish\(\);\s*return;/,
  'a thaw finishing after a page switch does not send a native view command');

function runEvaluate({ visible, frozen = false, overlayRect }) {
  const st = { busy: false, visible, frozen };
  const calls = { overlay: 0, freeze: 0, thaw: 0, drop: 0, clip: 0, retryReset: 0 };
  const perf = { overlayChecks: 0 };
  const vp = { x: 0, y: 0, w: 300, h: 200 };
  const document = {
    querySelectorAll() {
      return [{ getBoundingClientRect: () => overlayRect }];
    }
  };
  const functions = new Function(
    'st', 'perf', 'vpRect', 'document', 'OVERLAY_SEL', 'resetFreezeRetry',
    'freeze', 'thaw', 'dropSnap', 'kickClip', 'performance', 'freezeRetryAt',
    `${overlaySource}\n${evaluateSource}\nreturn { evaluate, overlayOverViewport };`
  )(
    st, perf, () => vp, document, '#toasts>.toast', () => { calls.retryReset++; },
    () => { calls.freeze++; }, () => { calls.thaw++; }, () => { calls.drop++; },
    () => { calls.clip++; }, { now: () => 1000 }, 0
  );
  functions.evaluate();
  const evaluateChecks = perf.overlayChecks;
  return { calls, perf, evaluateChecks, intersects: functions.overlayOverViewport(), st };
}

const bounds = (x, y, width, height) => ({ left: x, top: y, right: x + width, bottom: y + height, width, height });
const hiddenOverlay = runEvaluate({ visible: false, overlayRect: bounds(20, 20, 120, 50) });
assert.equal(hiddenOverlay.calls.freeze, 0, 'the Tasks page must never start freeze');
assert.equal(hiddenOverlay.evaluateChecks, 0, 'hidden native content does not even inspect overlays');
const offscreenToast = runEvaluate({ visible: true, overlayRect: bounds(320, 20, 100, 50) });
assert.equal(offscreenToast.intersects, false, 'a toast outside the viewport is not an overlapping overlay');
assert.equal(offscreenToast.calls.freeze, 0, 'a toast outside the site tab must not start freeze');
const overlappingToast = runEvaluate({ visible: true, overlayRect: bounds(250, 150, 100, 80) });
assert.equal(overlappingToast.intersects, true, 'an overlay intersecting the site viewport remains detectable');
assert.equal(overlappingToast.calls.freeze, 1, 'a toast covering the site tab still starts the required snapshot freeze');

const chromeMatch = bridge.match(/  window\.shelterShowChrome = function \(\) \{([\s\S]*?)\n  \};/);
assert.ok(chromeMatch, 'the UI-to-internal-page visibility hook must exist');
function runChromeHook(visible, frozen) {
  const state = { epoch: 0, visible, frozen, busy: false };
  const calls = [];
  const hook = new Function('st', 'resetFreezeRetry', 'setNativeContentClass', 'stopClipLoop', 'dropSnap', 'q',
    `return function shelterShowChrome() {${chromeMatch[1]}\n};`)(
    state,
    () => calls.push(['retry-reset']),
    () => calls.push(['class', state.visible]),
    () => calls.push(['clip-stop']),
    () => calls.push(['snapshot-drop']),
    method => { calls.push([method, state.visible]); return Promise.resolve({}); }
  );
  hook();
  return { state, calls };
}
const hiddenChrome = runChromeHook(false, false);
assert.equal(hiddenChrome.calls.some(call => typeof call[0] === 'string' && call[0].startsWith('view.')), false,
  'showing the Tasks page while already hidden must not call view.*');
const leavingSite = runChromeHook(true, true);
const hideCall = leavingSite.calls.find(call => call[0] === 'view.hide');
assert.ok(hideCall && hideCall[1] === true, 'the native view is hidden while the site is still marked visible, then visibility changes');
assert.equal(leavingSite.state.visible, false);

const zoomMatch = bridge.match(/  window\.shelterSetZoomAll = function \(f\) \{([\s\S]*?)\n  \};/);
assert.ok(zoomMatch, 'the shared zoom hook must exist');
const zoomState = { visible: false, zoom: 1, zoomPending: false };
const zoomCalls = [];
const setZoom = new Function('st', 'q', `return function (f) {${zoomMatch[1]}\n};`)(
  zoomState, (method, args) => { zoomCalls.push([method, args]); }
);
setZoom(1.25);
assert.equal(zoomCalls.length, 0, 'zoom changes are queued while the Tasks page is visible');
assert.equal(zoomState.zoomPending, true);
zoomState.visible = true;
setZoom(1.5);
assert.deepEqual(zoomCalls, [['view.zoom', { factor: 1.5 }]], 'a visible site tab receives the latest zoom value');
assert.equal(zoomState.zoomPending, false);
const openSource = extract(bridge, '  window.shelterOpen = function (url, opts) {', '  window.shelterShowChrome = function () {', 'site-tab open hook');
assert.ok(openSource.indexOf('st.visible = true') < openSource.indexOf("q('view.open'"),
  'opening a native site view must first mark the site tab visible');
assert.match(openSource, /if \(st\.zoomPending\)[\s\S]*opened\.then[\s\S]*st\.visible && st\.tabId === id[\s\S]*q\('view\.zoom'/,
  'a deferred zoom is applied only after a successful site-tab open');

const syncSource = extract(bridge, '  function syncTabs() {', '  function scheduleSync()', 'native tab synchronization');
assert.match(syncSource, /q\('tabs\.sync'/, 'tab cleanup remains immediate without using the view.* namespace');
assert.doesNotMatch(bridge, /q\('view\.sync'/);
assert.match(native, /if \(m == "tabs\.sync"\)/, 'the native handler accepts the lifecycle-only tab sync method');
const nativeViewCalls = Array.from(bridge.matchAll(/q\('(view\.[^']+)'/g), match => match[1]);
assert.deepEqual([...new Set(nativeViewCalls)].sort(), [
  'view.act', 'view.clip', 'view.hide', 'view.layout', 'view.open', 'view.show', 'view.zoom'
].sort(), 'only actual site-view operations remain in the view.* namespace');
const contextHook = extract(bridge, '    ctx: function (p) {', '    found: function (p) {', 'context-menu bridge hook');
assert.match(contextHook, /if \(!st\.visible \|\| !p \|\| p\.id !== st\.tabId/,
  'context actions are ignored when the native site tab is hidden or stale');
const ctxActSource = extract(bridge, '    ctxAct: function (o) {', '    openPlainWindow:', 'native context-action hook');
assert.match(ctxActSource, /if \(!st\.visible \|\| !o \|\| o\.id !== st\.tabId\)/,
  'native context actions cannot target a hidden or stale tab');
const ctxAct = new Function('st', 'q', `return function (o) {${ctxActSource.slice(ctxActSource.indexOf('{') + 1, ctxActSource.lastIndexOf('}') )}\n};`);
const actionCalls = [];
const hiddenAction = ctxAct({ visible: false, tabId: 'a' }, (method, args) => actionCalls.push([method, args]));
assert.equal(actionCalls.length, 0, 'hidden native context actions are not sent to the bridge');
assert.equal(await hiddenAction({ id: 'a', act: 'copy' }), null);
const visibleState = { visible: true, tabId: 'a' };
const sendCtxAction = ctxAct(visibleState, (method, args) => { actionCalls.push([method, args]); return Promise.resolve({ ok: true }); });
await sendCtxAction({ id: 'a', act: 'copy' });
assert.deepEqual(actionCalls, [['ctx.act', { id: 'a', act: 'copy' }]]);

const retryFactory = new Function('st', 'performance', 'setTimeout', 'clearTimeout', 'resetFreezeRetry', 'scheduleEval',
  `var freezeRetryAt = 0, freezeRetryTimer = 0;${retrySource}\nreturn { scheduleFreezeRetry, get retryAt() { return freezeRetryAt; } };`);
let retryTimers = 0;
let retryResets = 0;
const retry = retryFactory({ visible: false }, { now: () => 1000 }, () => { retryTimers++; return 1; }, () => {},
  () => { retryResets++; }, () => {});
retry.scheduleFreezeRetry();
assert.equal(retryTimers, 0, 'hidden native content does not schedule another freeze attempt');
assert.equal(retryResets, 1, 'a pending freeze retry is canceled when the native view is hidden');

console.log('Hidden-page native-view gating and offscreen-toast freeze tests passed');
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
