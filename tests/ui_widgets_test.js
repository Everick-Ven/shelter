'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');
const start = html.indexOf('/* =========================================================\n   ВИДЖЕТ · ПОМОДОРО');
const end = html.indexOf('\nfunction openWidgetPicker(anchor)', start);
assert(start >= 0 && end > start, 'the active Pomodoro and Efficiency Manager module must be present');
const moduleSource = html.slice(start, end);
const pickerStart = html.indexOf('function openWidgetPicker(anchor)');
const pickerEnd = html.indexOf('\nfunction openWidgetAddMenu(anchor)', pickerStart);
assert(pickerStart >= 0 && pickerEnd > pickerStart, 'the widget picker implementation must be present');
const pickerSource = html.slice(pickerStart, pickerEnd);
const tickerStart = html.indexOf('function initPomodoroClock() {');
const tickerEnd = html.indexOf('\nfunction pomoRenderTick', tickerStart);
assert(tickerStart >= 0 && tickerEnd > tickerStart, 'the Pomodoro clock must have one global lifecycle');
const tickerSource = html.slice(tickerStart, tickerEnd);
assert.match(tickerSource, /setInterval\(pomoTick, 1000\)/, 'the global clock ticks outside widget rendering');
assert(!html.includes('pomoCycleProgress') && !html.includes('pomoCycleTip') && !html.includes('pomo-cycle-dots'), 'the removed cycle indicator has no renderer or tooltip left');
const afterWidgetsStart = html.indexOf('AFTER.widgets = () => {');
const afterWidgetsEnd = html.indexOf('\n};', afterWidgetsStart);
assert(afterWidgetsStart >= 0 && afterWidgetsEnd > afterWidgetsStart);
assert(!html.slice(afterWidgetsStart, afterWidgetsEnd).includes('pomoTick'), 'widget remounts must not add duplicate Pomodoro tickers');

// Exercise the widget picker in isolation: each row is a real accessible switch,
// click state is persisted locally, and the global settings-switch handler is not used.
const pickerMenus = [];
let pickerPersists = 0, pickerRefreshes = 0;
const pickerButton = { id: 'wgShowBtn' };
const pickerContext = {
  S: { ui: { widgets: { pomo: true, eff: false } } },
  $(selector) { return selector === '#wgShowBtn' ? pickerButton : null; },
  esc: value => String(value),
  ico: name => `<svg data-icon="${name}"></svg>`,
  persist() { pickerPersists++; },
  refreshPage(name) { assert.equal(name, 'widgets'); pickerRefreshes++; },
  openMenu(anchor, _items, options) {
    const menu = { anchor, html: options.html, options, handler: null, addEventListener(type, handler) { assert.equal(type, 'click'); this.handler = handler; } };
    pickerMenus.push(menu);
    return menu;
  }
};
vm.runInNewContext(pickerSource + '\nglobalThis.__openWidgetPicker = openWidgetPicker;', pickerContext, { timeout: 3000 });
pickerContext.__openWidgetPicker(pickerButton);
assert.match(pickerMenus[0].html, /role="switch" aria-checked="true" aria-label="Помодоро"/, 'enabled widget switches expose the correct accessible state');
assert.match(pickerMenus[0].html, /role="switch" aria-checked="false" aria-label="Менеджер эффективности"/, 'disabled widget switches expose the correct accessible state');
assert.match(pickerMenus[0].html, /class="sw-t on"/, 'an enabled row uses the settings switch visual');
assert.match(pickerMenus[0].html, /class="sw-t"/, 'a disabled row uses the settings switch visual');
assert(!pickerMenus[0].html.includes('cbx') && !pickerSource.includes('data-sw'), 'widget switches avoid checkboxes and the global preferences handler');
const togglePickerRow = (menu, key) => menu.handler({
  target: { closest(selector) { assert.equal(selector, '[data-wg-toggle]'); return { dataset: { wgToggle: key } }; } },
  preventDefault() {}, stopPropagation() {}
});
togglePickerRow(pickerMenus[0], 'pomo');
assert.equal(pickerContext.S.ui.widgets.pomo, false, 'clicking the switch updates only the local widget preference');
assert.equal(pickerPersists, 1, 'widget visibility is persisted');
assert.equal(pickerRefreshes, 1, 'the widget page is refreshed after a visibility change');
assert.match(pickerMenus[1].html, /role="switch" aria-checked="false" aria-label="Помодоро"/, 'the refreshed menu announces the new switch state');
assert(!pickerMenus[1].html.includes('class="sw-t on"'), 'the refreshed switch visual matches its off state');
togglePickerRow(pickerMenus[1], 'eff');
assert.equal(pickerContext.S.ui.widgets.eff, true, 'the second switch is interactive too');
assert.equal(pickerPersists, 2);
assert.equal(pickerRefreshes, 2);

let clock = Date.UTC(2026, 9, 14, 12, 0, 0);
class TestDate extends Date {
  constructor(...args) { super(...(args.length ? args : [clock])); }
  static now() { return clock; }
}
const dayKey = value => {
  const d = new TestDate(value);
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
};

const harness = String.raw`
const S = globalThis.__state, T = globalThis.__transient;
const RENDER = {}, AFTER = {}, SEG = {}, OV = { stack: [] };
const document = globalThis.__document, window = globalThis.__window;
let _visitsDirty = false;
const DAY = 86400000;
const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
const pad2 = n => String(n).padStart(2, '0');
const dayKey = value => {
  const d = new Date(value);
  return String(d.getFullYear()) + '-' + String(d.getMonth() + 1).padStart(2, '0') + '-' + String(d.getDate()).padStart(2, '0');
};
const $ = selector => selector === '#effBody' ? globalThis.__effBody : null;
const ico = () => '';
const esc = value => String(value == null ? '' : value).replace(/[&<>"']/g, '');
const fmtDur = value => String(value);
const fmtDurS = value => String(Math.round(value)) + ' мин';
const segHtml = () => '<segmented-control>';
const emptyState = () => '<empty-state>';
const favFor = () => '<favorite>';
const curTab = () => globalThis.__tab;
const urlMeta = url => ({ kind: /^shelter\.dev\//.test(String(url)) ? 'internal' : 'web' });
const histNorm = url => String(url || '').trim().toLowerCase();
const hostOf = url => { try { return new URL(url).hostname.toLowerCase(); } catch (_) { return ''; } };
const siteInfo = host => ({ t: host, cat: host.includes('focus') ? 'Работа' : 'Нейтрально' });
const periodRange = period => {
  const end = new Date(Date.now()), start = new Date(end);
  start.setHours(0, 0, 0, 0);
  if (period === 'week') { start.setDate(start.getDate() - ((start.getDay() + 6) % 7)); }
  end.setHours(23, 59, 59, 999);
  return { from: start.getTime(), to: end.getTime() };
};
const saveNow = () => { globalThis.__counts.saves++; };
const persist = () => { globalThis.__counts.persists++; };
const refreshPage = () => { globalThis.__counts.refreshes++; };
const flushVisits = () => { globalThis.__counts.flushes++; _visitsDirty = false; };
const toast = message => { globalThis.__messages.push(String(message)); };
const confirmDlg = () => Promise.resolve(true);
const openPremiumDialog = () => {};
const openModal = () => ({ scrim: { addEventListener() {}, querySelector() { return null; } } });
const pageTimer = () => {};
`;

function boot(state = { ui: { widgets: { pomo: true, eff: true } }, visits: [], ghost: false }, options = {}) {
  const context = {
    __state: state,
    __transient: {},
    __document: { hidden: false, hasFocus: () => true, addEventListener() {} },
    __window: { addEventListener() {}, localStorage: options.localStorage || { getItem() { return null; }, setItem() {} }, AudioContext: options.AudioContext || null, shelterNative: options.native || null },
    __tab: options.tab || null,
    __effBody: { innerHTML: '' },
    __counts: { saves: 0, persists: 0, flushes: 0, refreshes: 0 },
    __messages: [],
    Date: TestDate,
    URL,
    setInterval: () => 0,
    setTimeout,
    clearTimeout,
    console
  };
  vm.runInNewContext(harness + '\n' + moduleSource + `
globalThis.__api = {
  POMO_DUR, widgetPrefs, pomoDur, pomoState, pomoWidgetHtml, pomoGlobalButtonHtml, pomoToggle, pomoChangeMode, pomoReset, pomoSkip,
  pomoFinishPhase, pomoProcessExpired, pomoAcknowledgeNotice, pomoMergeSnapshot, pomoSignal,
  efficiencyCanTrack, efficiencyCurrentVisit, efficiencyAddActiveMs,
  efficiencyPendingByDay, efficiencyFlushActiveSegment, efficiencyTrackingSync,
  efficiencyCheckpoint, effData, effBodyHtml, effWidgetHtml, effPeriod, periodRange, dayKey, render: RENDER, segments: SEG, transient: T
};`, context, { timeout: 5000 });
  return { context, state, api: context.__api };
}

(async () => {
  const first = boot();
  const prefs = first.api.widgetPrefs().pomodoro;
  assert.equal(prefs.workMinutes, 25);
  assert.equal(prefs.shortBreakMinutes, 5);
  assert.equal(prefs.longBreakMinutes, 15);
  assert.equal(prefs.autoBreak, false);
  assert.equal(prefs.soundEnabled, true, 'the completion sound is enabled by default');
  assert.equal(prefs.session.mode, 'work');
  assert.equal(prefs.session.remain, 25 * 60);
  assert.deepEqual({ ...first.api.POMO_DUR }, { work: 1500, short: 300, long: 900 });

  prefs.workMinutes = 14; prefs.shortBreakMinutes = 0; prefs.longBreakMinutes = 4;
  first.api.widgetPrefs();
  assert.equal(prefs.workMinutes, 15, 'legacy focus values are clamped to the accepted minimum');
  assert.equal(prefs.shortBreakMinutes, 1, 'legacy short breaks are clamped to one minute');
  assert.equal(prefs.longBreakMinutes, 5, 'legacy long breaks are clamped to five minutes');
  prefs.workMinutes = 61; prefs.shortBreakMinutes = 6; prefs.longBreakMinutes = 31;
  first.api.widgetPrefs();
  assert.equal(prefs.workMinutes, 60);
  assert.equal(prefs.shortBreakMinutes, 5);
  assert.equal(prefs.longBreakMinutes, 30);
  assert.equal(first.api.pomoDur('short'), 5 * 60);
  assert.equal(first.api.pomoDur('long'), 30 * 60);
  prefs.workMinutes = 25; prefs.shortBreakMinutes = 5; prefs.longBreakMinutes = 15;
  const widgetPage = first.api.render.widgets();
  assert(widgetPage.includes('class="page"') && widgetPage.includes('class="wg-grid"'), 'the Widgets page renders its configured cards');
  assert(widgetPage.includes('wg-tile pomo-tile'), 'the Pomodoro card has a concrete renderer');
  assert(widgetPage.includes('id="pomoToggleBtn"') && widgetPage.includes('data-act="pomoSettings"'), 'Pomodoro start and settings controls are present in the page');
  assert(widgetPage.includes('с помощью переключателей'), 'the widget toolbar describes the new switch controls');
  assert(moduleSource.includes('и включите нужные виджеты.'), 'the empty state uses switch terminology too');
  assert(!/pomo-cycles|pomo-cycle-dots|pomoCycles|До длинного перерыва|циклов сегодня/i.test(widgetPage), 'the Pomodoro card has no visible cycle dots, statistic, or tooltip');
  assert(widgetPage.includes('data-act="pomoSkip"'), 'the next-phase control remains available after removing the decorative cycle indicator');
  const globalIndicator = first.api.pomoGlobalButtonHtml();
  assert(globalIndicator.includes('id="pomoGlobalBtn"') && globalIndicator.includes('pomo-global-ring'), 'the global toolbar indicator renders its progress ring');
  assert(globalIndicator.includes('aria-haspopup="dialog"') && globalIndicator.includes('aria-controls="pomoPopover"'), 'the global indicator has an accessible popover relationship');
  const pomoSettings = moduleSource.slice(moduleSource.indexOf('async function openPomodoroSettings()'));
  assert(pomoSettings.includes('id="pomoSoundEnabled"') && pomoSettings.includes('pref.soundEnabled'), 'the default-on sound preference is user-configurable');
  assert(pomoSettings.includes("['#pomoShortMinutes', 1, 5") && pomoSettings.includes("['#pomoLongMinutes', 5, 30"), 'settings validation matches the accepted minute ranges');
  assert(widgetPage.includes('id="effBody"'), 'the Efficiency Manager card renders with its update target');

  const skippedPhase = boot();
  await skippedPhase.api.pomoSkip();
  const afterSkip = skippedPhase.api.pomoState();
  assert.equal(afterSkip.mode, 'short', 'the Next button still advances the Pomodoro phase');
  assert.equal(afterSkip.cycles, 1, 'the internal phase counter remains for long-break scheduling');
  assert(!skippedPhase.api.pomoWidgetHtml().includes('pomo-cycle-dots'), 'internal phase scheduling is not exposed as a visual statistic');

  const startedAt = clock;
  first.api.pomoToggle();
  assert.equal(prefs.session.running, true);
  assert.equal(prefs.session.endsAt, startedAt + 25 * 60 * 1000);
  clock += 60_000;
  assert.equal(first.api.pomoState().remain, 24 * 60);
  first.api.pomoToggle();
  assert.equal(prefs.session.running, false, 'pause stops the timestamp countdown');
  assert.equal(prefs.session.remain, 24 * 60, 'pause stores the exact remaining seconds');
  assert.equal(prefs.session.endsAt, 0);
  clock += 90_000;
  first.api.pomoToggle();
  assert.equal(prefs.session.running, true, 'resume starts from the paused remainder');
  assert.equal(prefs.session.endsAt, clock + 24 * 60 * 1000);
  const reloadedState = JSON.parse(JSON.stringify(first.state));
  clock += 42_000;
  const reloaded = boot(reloadedState);
  assert.equal(reloaded.api.pomoState().remain, 23 * 60 + 18, 'reload computes remaining time from endsAt, not the stale saved counter');

  const phase = reloaded.api.widgetPrefs().pomodoro;
  phase.autoBreak = false;
  phase.session = { mode: 'work', remain: 1, running: true, endsAt: clock - 1000, cycles: 3, cycleDay: dayKey(clock) };
  let session = reloaded.api.pomoState();
  assert.equal(session.mode, 'long', 'every fourth completed focus phase selects the long break');
  assert.equal(session.cycles, 4);
  assert.equal(session.running, false, 'auto-break stays off by default when a work phase expires');
  assert.equal(session.remain, 15 * 60);

  phase.autoBreak = true;
  phase.session = { mode: 'work', remain: 1, running: true, endsAt: clock - 60_000, cycles: 0, cycleDay: dayKey(clock) };
  session = reloaded.api.pomoState();
  assert.equal(session.mode, 'short');
  assert.equal(session.running, true, 'an enabled automatic break also starts after an elapsed phase is restored');
  assert.equal(session.remain, 4 * 60, 'time spent since the phase boundary is deducted from the break');
  clock += 5 * 60_000;
  session = reloaded.api.pomoState();
  assert.equal(session.mode, 'work', 'an elapsed break advances back to work after reload');
  assert.equal(session.running, false);

  phase.autoBreak = true;
  phase.session = { mode: 'work', remain: 1, running: true, endsAt: clock - 22 * 60_000, cycles: 0, cycleDay: dayKey(clock) };
  session = reloaded.api.pomoState();
  assert.equal(session.mode, 'work');
  assert.equal(session.running, false, 'catch-up pauses after advancing through an expired automatic break');
  assert.deepEqual(Array.from(session.pendingNotice.completedModes), ['work', 'short'], 'long-idle catch-up records every phase that elapsed');
  const noticeId = session.pendingNotice.id;
  const updatedAt = session.updatedAt;
  assert.equal(reloaded.api.pomoState().pendingNotice.id, noticeId, 'repeated ticks do not create duplicate completion notices');
  assert.equal(session.updatedAt, updatedAt, 'a persisted completion notice is stable across timer ticks');
  await reloaded.api.pomoAcknowledgeNotice();
  assert.equal(session.pendingNotice, null, 'the completion indicator remains until explicit acknowledgement');

  phase.session = { mode: 'work', remain: 1, running: true, endsAt: clock - 60 * 60_000, cycles: 0, cycleDay: dayKey(clock) };
  session = reloaded.api.pomoState();
  assert.equal(session.pendingNotice.completedCount, 2, 'a long idle period is summarized as one notice for every phase that elapsed');
  assert.equal(reloaded.api.pomoState().pendingNotice.id, session.pendingNotice.id, 'long-idle ticks keep the same notice identity');

  const sharedValues = new Map(), sharedStorage = {
    getItem(key) { return sharedValues.has(key) ? sharedValues.get(key) : null; },
    setItem(key, value) { sharedValues.set(key, String(value)); }
  };
  const audioEvents = [];
  class TestAudioContext {
    constructor() { this.state = 'running'; this.currentTime = 0; this.destination = {}; }
    createOscillator() { return { frequency: { setValueAtTime() {} }, connect() {}, start() { audioEvents.push('start'); }, stop() {} }; }
    createGain() { return { gain: { setValueAtTime() {}, exponentialRampToValueAtTime() {} }, connect() {} }; }
  }
  const simultaneousState = { ui: { widgetPrefs: { pomodoro: { soundEnabled: true, session: { mode: 'work', remain: 1, running: true, endsAt: clock - 1000, cycles: 0, cycleDay: dayKey(clock) } } } }, visits: [], ghost: false };
  const firstWindow = boot(simultaneousState, { localStorage: sharedStorage, AudioContext: TestAudioContext });
  const firstWindowSession = firstWindow.api.pomoState();
  assert.equal(audioEvents.length, 2, 'the focused window plays one two-tone completion signal');
  const secondWindow = boot(JSON.parse(JSON.stringify(firstWindow.state)), { localStorage: sharedStorage, AudioContext: TestAudioContext });
  secondWindow.api.pomoSignal(firstWindowSession.pendingNotice.id);
  assert.equal(audioEvents.length, 2, 'another page/window suppresses the already-claimed signal');

  phase.session = { mode: 'work', remain: 25 * 60, running: false, endsAt: 0, cycles: 2, cycleDay: dayKey(clock) };
  await reloaded.api.pomoChangeMode();
  assert.equal(phase.session.mode, 'short', 'manual phase changes update the saved session');
  phase.session.mode = 'long'; phase.session.remain = 45; phase.session.cycles = 2;
  await reloaded.api.pomoReset();
  assert.equal(phase.session.mode, 'work');
  assert.equal(phase.session.remain, 25 * 60);
  assert.equal(phase.session.cycles, 0);

  const visit = { tab: 'tab-1', url: 'https://focus.example/work', at: clock, activeMs: 0, activeMsByDay: {} };
  const tracking = boot({ ui: { widgets: { pomo: true, eff: true } }, visits: [visit], ghost: false }, { tab: { id: 'tab-1', url: visit.url } });
  tracking.api.efficiencyTrackingSync();
  assert.equal(tracking.api.transient.effActiveVisit, visit);
  clock += 10_000;
  tracking.api.efficiencyCheckpoint();
  assert.equal(visit.activeMs, 10_000);
  assert.equal(visit.activeMsByDay[dayKey(clock)], 10_000);
  assert(tracking.context.__counts.flushes > 0, 'active-time checkpoints persist visit data');

  const midnightVisit = { activeMs: 0, activeMsByDay: {} };
  const midnight = new Date(clock); midnight.setHours(23, 59, 59, 500);
  tracking.api.efficiencyAddActiveMs(midnightVisit, midnight.getTime(), 1500);
  const nextDay = new Date(midnight); nextDay.setDate(nextDay.getDate() + 1);
  assert.equal(midnightVisit.activeMsByDay[dayKey(midnight.getTime())], 500);
  assert.equal(midnightVisit.activeMsByDay[dayKey(nextDay.getTime())], 1000, 'activity is split at the local calendar-day boundary');

  const oldVisit = { url: 'https://legacy.example/path', at: clock, dur: 12 };
  const legacy = boot({ ui: { widgets: { pomo: true, eff: true } }, visits: [oldVisit], ghost: false });
  const legacyData = legacy.api.effData('day');
  assert.equal(legacyData.estimatedTotal, 12, 'legacy dur is treated as an estimate, not a screen-active measurement');
  assert(legacy.api.effBodyHtml(legacyData).includes('приблизительные данные'));
  assert(legacy.api.effBodyHtml(legacyData).includes('не системный Screen Time'));
  legacy.api.segments.effPeriod('week');
  assert.equal(legacy.state.ui.widgetPrefs.efficiency.period, 'week', 'the selected Efficiency period persists');
  assert(legacy.context.__counts.persists > 0);
  assert(legacy.context.__counts.refreshes === 0, 'period changes do not redraw the whole page');
  assert(legacy.context.__effBody.innerHTML.length > 0, 'period updates replace only the widget body');

  const guard = boot({ ui: { widgets: { pomo: true, eff: true } }, visits: [], ghost: false }, { tab: { id: 'tab-1', url: 'https://focus.example' } });
  assert.equal(guard.api.efficiencyCanTrack(), true, 'basic widget tracking is available without Premium');
  guard.api.pomoToggle();
  assert.equal(guard.api.widgetPrefs().pomodoro.session.running, true, 'Pomodoro controls remain interactive without a paid entitlement');
  guard.context.__document.hidden = true;
  assert.equal(guard.api.efficiencyCanTrack(), false, 'hidden documents are not tracked');
  guard.context.__document.hidden = false;
  guard.api.transient.effWindowFocused = false;
  assert.equal(guard.api.efficiencyCanTrack(), false, 'unfocused application windows are not tracked');
  guard.api.transient.effWindowFocused = true;
  guard.state.ui.widgets.eff = false;
  assert.equal(guard.api.efficiencyCanTrack(), false, 'a hidden Efficiency widget is not tracked');

  const premiumStart = html.indexOf('function isDevelopmentBuild()');
  const premiumEnd = html.indexOf('\nfunction premiumUntilLabel', premiumStart);
  assert(premiumStart >= 0 && premiumEnd > premiumStart, 'the active Premium entitlement module must be present');
  const premiumSource = html.slice(premiumStart, premiumEnd);
  const premiumHarness = String.raw`
const S = globalThis.__state, T = globalThis.__transient, window = globalThis.__window;
const DAY = 86400000;
const renderUser = () => {};
const refreshPage = () => {};
const profileActivity = () => ({});
const storedProfileUnlocks = () => new Set();
const profileAchievementStatuses = () => ({});
const notifyNewProfileAchievements = () => {};
`;
  function bootPremium(options = {}) {
    const context = {
      __state: { ui: options.ui || {} },
      __transient: options.transient || {},
      __window: { shelterNative: options.native || null },
      Date: TestDate,
      console
    };
    vm.runInNewContext(premiumHarness + '\n' + premiumSource + `\nglobalThis.__premiumApi = { isDevelopmentBuild, isBetaBuild, canRedeemPromo, premiumState, refreshPremiumEntitlement, transient: T };`, context, { timeout: 5000 });
    return { context, api: context.__premiumApi };
  }
  const release = bootPremium({
    ui: { premium: { active: true } },
    transient: { premiumMockEntitlement: { active: true } },
    native: { isDevelopment: false, distributionChannel: 'direct' }
  });
  assert.equal(release.api.isDevelopmentBuild(), false);
  assert.equal(release.api.premiumState().active, false, 'legacy local Premium and Debug mock data do not grant Release access');
  release.context.__state.ui.betaPromoEntitlement = { active: true, until: clock + 60_000 };
  assert.equal(release.api.premiumState().active, false, 'a Beta promo entitlement does not activate a stable Release build');
  const beta = bootPremium({
    ui: { betaPromoEntitlement: { active: true, plan: 'monthly', until: clock + 60_000 } },
    native: { isDevelopment: false, distributionChannel: 'beta' }
  });
  assert.equal(beta.api.isDevelopmentBuild(), false, 'Beta Release builds do not enable Debug mocks');
  assert.equal(beta.api.isBetaBuild(), true);
  assert.equal(beta.api.canRedeemPromo(), true);
  assert.equal(beta.api.premiumState().source, 'beta-promo', 'a locally redeemed promo works in explicitly marked Beta builds');
  const debug = bootPremium({
    transient: { premiumMockEntitlement: { active: true, until: clock + 60_000 } },
    native: { isDevelopment: true, distributionChannel: 'direct' }
  });
  assert.equal(debug.api.isDevelopmentBuild(), true);
  assert.equal(debug.api.premiumState().source, 'dev-mock', 'the temporary mock is available only in direct Debug builds');
  debug.context.__window.shelterNative.distributionChannel = 'store';
  assert.equal(debug.api.isDevelopmentBuild(), false);
  assert.equal(debug.api.premiumState().active, false, 'a non-direct channel cannot activate the Debug mock');
  release.api.transient.premiumServerEntitlement = { verified: false, active: true };
  assert.equal(release.api.premiumState().active, false, 'an unverified server payload fails closed');
  release.api.transient.premiumServerEntitlement = { verified: true, active: true, until: clock + 60_000 };
  assert.equal(release.api.premiumState().active, true, 'a verified, active, unexpired server entitlement grants access');

  const requests = [];
  const racing = bootPremium({ native: {
    isDevelopment: false,
    distributionChannel: 'direct',
    getPremiumEntitlement: () => new Promise(resolve => requests.push(resolve))
  } });
  const staleRequest = racing.api.refreshPremiumEntitlement();
  const latestRequest = racing.api.refreshPremiumEntitlement();
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(requests.length, 2);
  requests[1]({ ok: true, entitlement: { verified: true, status: 'active', planId: 'monthly', expiresAt: clock + 60_000, paidOrderCount: 1 } });
  await latestRequest;
  assert.equal(racing.api.premiumState().active, true);
  assert.equal(racing.api.transient.premiumServerEntitlement.firstPaymentConfirmed, true, 'payment history comes from the verified server response');
  requests[0]({ ok: false, verified: false, status: 'unavailable' });
  await staleRequest;
  assert.equal(racing.api.premiumState().active, true, 'a stale verification result cannot overwrite the latest entitlement');
  racing.context.__window.shelterNative = null;
  await racing.api.refreshPremiumEntitlement();
  assert.equal(racing.api.transient.premiumServerEntitlement, null, 'missing native verification clears cached access');
  assert.equal(racing.api.premiumState().active, false);

  const profile = html.slice(html.indexOf('function storedProfileUnlocks()'), html.indexOf('function captureProfileUnlocks()'));
  assert(profile.includes("id !== 'firstPayment' || paymentVerified"), 'a legacy local first-payment unlock requires a verified server record');
  assert(html.includes('Первая оплата Premium подтверждена платёжным сервером.'), 'the achievement describes a server-confirmed payment');
  const paymentReturn = html.slice(html.indexOf('function returnFromPaymentGateway()'), html.indexOf('function openProfileSettings()', html.indexOf('function returnFromPaymentGateway()')));
  assert(paymentReturn.includes('isDevelopmentBuild() && !!T.profilePaymentStarted'));
  assert(paymentReturn.includes('T.premiumMockEntitlement =') && !paymentReturn.includes('T.premiumServerEntitlement ='), 'a demo return can create only a temporary Debug mock');
  assert(html.includes('const FREE_SPACE_LIMIT = 3;'));
  assert.equal((html.match(/if \(isNew && !isPremium\(\) && Object\.keys\(S\.spaces\)\.length >= FREE_SPACE_LIMIT\)/g) || []).length, 2, 'the free-space limit is enforced when opening and submitting the create form');
  const promoActivation = html.slice(html.indexOf('function activatePromoKey()'), html.indexOf('RENDER.profile =', html.indexOf('function activatePromoKey()')));
  assert(promoActivation.includes('if (!canRedeemPromo())'));
  assert(promoActivation.includes("S.ui.betaPromoEntitlement = entitlement; saveNow();"), 'Beta promos persist separately from Debug-only mock access');
  assert(promoActivation.includes("source: betaPromo ? 'beta-promo' : 'dev-mock'"));
  const scheme = fs.readFileSync(path.join(__dirname, '..', 'src', 'ui_scheme.cc'), 'utf8');
  assert(scheme.includes('ReplaceAll(&data, "__DEV_BUILD__", "false")') && scheme.includes('ReplaceAll(&data, "__DEV_BUILD__", "true")'));
  assert(scheme.includes('ReplaceAll(&data, "__DISTRIBUTION_CHANNEL__", "direct")'));
  assert(scheme.includes('ReplaceAll(&data, "__DISTRIBUTION_CHANNEL__", "beta")'));
  const cmake = fs.readFileSync(path.join(__dirname, '..', 'CMakeLists.txt'), 'utf8');
  assert(cmake.includes('option(SHELTER_BETA_BUILD') && cmake.includes('SHELTER_BETA_BUILD=1'));
  const workflow = fs.readFileSync(path.join(__dirname, '..', '.github', 'workflows', 'build.yml'), 'utf8');
  assert.equal(
    (workflow.match(/-DSHELTER_BETA_BUILD=/g) || []).length,
    4,
    'the Beta channel is configured for release and CI-hook builds on both platforms'
  );
  assert.equal((workflow.match(/GITHUB_REF_TYPE:-branch/g) || []).length, 2, 'tagged production builds disable the Beta channel');

  console.log('Pomodoro, Efficiency Manager, and Premium regression tests passed');
})().catch(error => { console.error(error); process.exit(1); });
