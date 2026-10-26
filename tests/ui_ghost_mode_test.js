'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'resources/ui/index.html'), 'utf8');
const hostBridge = fs.readFileSync(path.join(root, 'resources/ui/host-bridge.js'), 'utf8');

const storeStart = html.indexOf('function spacesForStore()');
const storeEnd = html.indexOf('function saveNow()', storeStart);
assert(storeStart >= 0 && storeEnd > storeStart, 'ghost-aware tab persistence helpers must exist');
const ghostStart = html.indexOf('function closeGhostTabs()');
const ghostEnd = html.indexOf('async function fire()', ghostStart);
assert(ghostStart >= 0 && ghostEnd > ghostStart, 'ghost mode transition functions must exist');
const tabInfoStart = html.indexOf('window.__shelterTab =');
const tabInfoEnd = html.indexOf('window.toggleGhost =', tabInfoStart);
assert(tabInfoStart >= 0 && tabInfoEnd > tabInfoStart, 'native tab-partition lookup must exist');

let nextTabId = 0;
const activeTab = { id: 'active-persistent', url: 'https://example.test/private', title: 'Private page', stack: ['https://example.test/private'], ghost: false };
const untouchedTab = { id: 'other-tab', url: 'https://other.test/', title: 'Other page', stack: ['https://other.test/'], ghost: false };
const context = {
  S: {
    ghost: false,
    spaces: { work: { name: 'Work', tabs: [activeTab, untouchedTab] } },
    currentSpace: 'work',
    activeTabId: activeTab.id,
    closed: [],
    downloads: []
  },
  window: {},
  curTab() { return activeTab; },
  profileAchievementCheckpoint() { return { d: {} }; },
  captureProfileUnlocks() {},
  reconcileProfileAchievementChange() {},
  syncSwitches() {},
  updateToolbar() {},
  renderHeroBits() {},
  refreshPage() {},
  renderTabs() {},
  confirmDlg: async () => true,
  newTabObj(url) { return { id: 'after-ghost-' + (++nextTabId), url, title: '', stack: [url], ghost: !!context.S.ghost }; },
  toast() {},
  events: [],
  renderView(animate, direction) { context.events.push({ type: 'renderView', animate, direction, ghost: context.S.ghost, activeTabId: context.S.activeTabId }); },
  saveNow() { context.saved = { spaces: context.spacesForStore(), activeTabId: context.activeTabIdForStore() }; }
};
context.window.shelterGhostSync = () => context.events.push({ type: 'ghostSync' });
context.window.shelterNative = { ghostSetEnabled: on => context.events.push({ type: 'nativeGhost', on }) };
vm.createContext(context);
vm.runInContext(html.slice(storeStart, storeEnd), context, { filename: 'ghost-store-helpers.js' });
vm.runInContext(html.slice(ghostStart, ghostEnd), context, { filename: 'ghost-mode.js' });
vm.runInContext(html.slice(tabInfoStart, tabInfoEnd), context, { filename: 'ghost-tab-info.js' });

(async () => {
  assert.strictEqual(await context.toggleGhost(true), true);
  assert.strictEqual(context.S.ghost, true);
  assert.strictEqual(activeTab.ghost, true, 'the already-open tab is marked private before persistence');
  assert(!context.saved.spaces.work.tabs.some(tab => tab.id === activeTab.id), 'the active tab URL/stack/title are omitted from local storage when ghost mode starts');
  assert.strictEqual(context.saved.activeTabId, '', 'the active ghost tab is not saved as the persistent active tab');
  assert(context.events.some(event => event.type === 'renderView' && event.ghost), 'the active native view is rebound immediately instead of waiting for navigation');

  const switched = context.window.__shelterTab(untouchedTab.id);
  assert.strictEqual(switched.ghost, true, 'a pre-existing tab becomes private when opened during ghost mode');
  assert.strictEqual(untouchedTab.ghost, true, 'tab-partition lookup records the privacy state for later filtering');
  context.saveNow();
  assert(context.saved.spaces.work.tabs.every(tab => ![activeTab.id, untouchedTab.id].includes(tab.id)), 'no tab opened during ghost mode is written to the persistent tab list');
  assert(context.saved.spaces.work.tabs.every(tab => tab.url === 'shelter.dev/start'), 'a harmless empty start tab may remain to preserve the space structure');

  assert.strictEqual(await context.toggleGhost(false), true);
  assert.strictEqual(context.S.ghost, false);
  assert.strictEqual(context.S.spaces.work.tabs.length, 1, 'ghost tabs are closed when ghost mode ends');
  assert(context.S.spaces.work.tabs[0].id.startsWith('after-ghost-'));
  assert(context.events.some(event => event.type === 'renderView' && !event.ghost), 'the native view is rebound to a persistent context when ghost mode ends');
  assert(hostBridge.includes("var partition = (info.ghost ? 'temp:ghost-' : 'persist:space-') + info.space;"), 'the bridge maps ghost tabs to memory-only CEF partitions');

  console.log('ghost mode persistence and partition transition regression test passed');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
