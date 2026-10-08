'use strict';

// Regression coverage for the ad/tracker statistics path without launching CEF.
//
// The CEF event dispatcher lives in the first (adapter) inline script, while the
// application state (S, curTab, persist, …) lives in the second one. A handler
// written in the adapter script therefore cannot touch the application state —
// that is exactly how the "blocked" statistics silently stopped working (the
// try/catch around the switch swallowed the ReferenceError). This test keeps the
// wiring honest: the adapter must delegate to window hooks, the hooks must live
// in the application script, and their accounting must be correct.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(
  path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');

const scripts = [];
for (let at = html.indexOf('<script'); at >= 0; at = html.indexOf('<script', at + 1)) {
  const start = html.indexOf('>', at) + 1;
  const end = html.indexOf('</script>', start);
  if (end < 0) break;
  scripts.push(html.slice(start, end));
}
const adapter = scripts.find(b => b.indexOf('window.shelterCefDispatch =') >= 0);
const app = scripts.find(b => b.indexOf('const S = {') >= 0 && b.indexOf('window.shelterTest') >= 0);
assert.ok(adapter, 'CEF adapter script is present');
assert.ok(app, 'application script is present');
assert.notEqual(adapter, app, 'adapter and application code live in different scripts');

// --- адаптер не имеет права трогать внутренности приложения ------------------
const dispatchStart = adapter.indexOf('window.shelterCefDispatch =');
assert.ok(dispatchStart >= 0);
const dispatchEnd = adapter.indexOf("window.addEventListener('load'", dispatchStart);
const dispatch = adapter.slice(dispatchStart, dispatchEnd > 0 ? dispatchEnd : undefined);
for (const needle of ['S.prefs', 'S.blocks', 'curTab(', 'dayKey(', 'persist()', 'favCachePut(', 'shelterBlockedStats(']) {
  const offender = needle === 'shelterBlockedStats('
    ? /shelterBlockedStats\(/.test(dispatch) && !/typeof window\.shelterBlockedStats === 'function'/.test(dispatch)
    : dispatch.indexOf(needle) >= 0;
  assert.ok(!offender, `dispatcher must not use ${needle} directly`);
}
for (const hook of ['shelterBlockedStats', 'shelterStrictBlock', 'shelterFavIcon']) {
  assert.match(
    dispatch, new RegExp(`typeof window\\.${hook} === 'function'`),
    `dispatcher delegates ${hook} to the application script`);
}

// --- хуки определены в скрипте приложения и не потерялись --------------------
for (const hook of ['shelterBlockedStats', 'shelterStrictBlock', 'shelterFavIcon']) {
  assert.match(app, new RegExp(`window\\.${hook} = `), `application defines window.${hook}`);
}

// --- логика учёта -------------------------------------------------------------
const hookStart = app.indexOf('const tabById = id => {');
const hookEnd = app.indexOf('window.shelterFavIcon');
assert.ok(hookStart >= 0 && hookEnd > hookStart, 'accounting hooks are extractable');
const hookSource = app.slice(hookStart, hookEnd);

function makeContext() {
  const state = {
    spaces: {
      main: { tabs: [
        { id: 't-active', url: 'https://active.example/', blocked: 0 },
        { id: 't-ad', url: 'http://127.0.0.1:1/adpage', blocked: 0 },
      ] },
    },
    currentSpace: 'main',
    activeTabId: 't-active',
    prefs: { trackers: true, fp: true, https: true, strict: false },
    blocks: {},
  };
  const calls = { persist: 0, raf: 0, synced: 0, hero: 0 };
  const toasts = [];
  const context = {
    S: state,
    curTab: () => state.spaces.main.tabs.find(t => t.id === state.activeTabId),
    dayKey: () => '2026-10-08',
    persist: () => { calls.persist += 1; },
    raf: fn => { calls.raf += 1; fn(); },
    syncControls: () => { calls.synced += 1; },
    renderHeroFoot: () => { calls.hero += 1; },
    fmtN: n => String(n),
    todayBlocked: () => 0,
    $: () => null,
    $$: () => [],
    toasts,
    toast: (msg, opts) => { toasts.push({ msg, opts }); },
    favCachePut: () => {},
    window: null,
  };
  context.window = context;
  vm.createContext(context);
  vm.runInContext(
    hookSource + '\nglobalThis.__hooks = { blocked: window.shelterBlockedStats, strict: window.shelterStrictBlock };',
    context, { filename: 'resources/ui/index.html' });
  return { state, calls, hooks: context.__hooks, toasts };
}

{
  const { state, calls, hooks } = makeContext();
  hooks.blocked({ host: 'ads.example', a: 1, id: 't-ad' });
  hooks.blocked({ host: 'tracker.example', t: 1, id: 't-ad' });
  hooks.blocked({ f: 1 });                           // без id — в активную вкладку
  const ad = state.spaces.main.tabs.find(t => t.id === 't-ad');
  const active = state.spaces.main.tabs.find(t => t.id === 't-active');
  assert.equal(ad.blocked, 2, 'blocked requests are credited to their own tab');
  assert.equal(active.blocked, 1, 'events without a tab id fall back to the active tab');
  assert.equal(state.blocks['2026-10-08'].a, 1, 'ad blocks land in the daily tally');
  assert.equal(state.blocks['2026-10-08'].t, 1, 'tracker blocks land in the daily tally');
  assert.equal(state.blocks['2026-10-08'].f, 1, 'fingerprint defenses land in the daily tally');
  assert.ok(calls.persist > 0 && calls.raf > 0 && calls.synced > 0 && calls.hero > 0,
    'statistics trigger persist and UI refresh');
}

// --- выключенный тумблер не даёт статистики ----------------------------------
{
  const { state, hooks } = makeContext();
  state.prefs.trackers = false;
  hooks.blocked({ a: 1, t: 1, id: 't-ad' });
  assert.equal(state.spaces.main.tabs.find(t => t.id === 't-ad').blocked, 0,
    'blocks are ignored while the blocker is off');
  assert.equal(Object.keys(state.blocks).length, 0, 'daily tally stays empty while off');
  hooks.blocked({ f: 1 });                           // анти-отпечаток ещё включён
  assert.equal(state.blocks['2026-10-08'].f, 1, 'other protections keep counting');
}

// --- строгий режим: тост только при включённом тумблере и не чаще раза в 6 с --
{
  const { state, hooks, toasts } = makeContext();
  state.prefs.strict = false;
  hooks.strict({ host: 'cdn.example', script: 'ads.js' });
  assert.equal(toasts.length, 0, 'no strict toast while the toggle is off');
  state.prefs.strict = true;
  hooks.strict({ host: 'cdn.example', script: 'ads.js' });
  assert.equal(toasts.length, 1, 'strict toast explains the blocked script');
  assert.match(toasts[0].msg, /Строгий режим/);
  assert.equal(typeof toasts[0].opts.action.run, 'function', 'the toast can allow the site');
  hooks.strict({ host: 'cdn.example', script: 'ads.js' });
  assert.equal(toasts.length, 1, 'repeat blocks on the same host stay silent');
  hooks.strict({ host: 'other.example', script: 'x.js' });
  assert.equal(toasts.length, 2, 'другой сайт получает свой тост');
}

console.log('BLOCKED_STATS_TEST_PASS');
