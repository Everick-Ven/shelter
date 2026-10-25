'use strict';

// P0-1: saveNow() не должен переписывать localStorage целиком на каждом
// сохранении. Пины закрывают два приёма из ТЗ:
//  1) store.set хранит последнюю УСПЕШНО записанную строку по ключу и
//     пропускает запись при совпадении; исключение не помечает запись;
//  2) shelter:lux не парсится заново на каждый saveNow — разобранный объект
//     живёт в памяти и инвалидируется при внешнем изменении ключа.
// Побочные вызовы сохранения (wpAfterTaskSave, flushVisits, достижения,
// blobToStore/pwToStore) обязаны остаться, а close/hidden — сбрасывать
// отложенное сохранение вместе с визитами.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(
  path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf-8');

// --- извлечение настоящего store из приложения ------------------------------
const storeStart = html.indexOf('const store = (() => {');
assert.ok(storeStart >= 0, 'store block found');
const storeEnd = html.indexOf('})();', storeStart) + '})();'.length;
const storeSrc = html.slice(storeStart, storeEnd);
assert.ok(storeSrc.includes('const written = {}'), 'store keeps last written strings');

function makeLs({ failKeys = new Set() } = {}) {
  const data = new Map();
  const calls = { set: 0, get: 0, remove: 0 };
  return {
    calls, data,
    getItem(k) { calls.get++; return data.has(k) ? data.get(k) : null; },
    setItem(k, v) {
      calls.set++;
      if (failKeys.has(k)) throw new Error('quota');
      data.set(k, String(v));
    },
    removeItem(k) { calls.remove++; data.delete(k); },
  };
}

function loadStore(ls) {
  const sandbox = { window: { localStorage: ls } };
  vm.runInNewContext(storeSrc + '\nthis.__store = store;', sandbox);
  if (ls) ls.calls.set = ls.calls.get = ls.calls.remove = 0; // без пробного записи инициализации
  return sandbox.__store;
}

// --- 1) пропуск одинаковой записи, пометка только после успеха -------------
{
  const ls = makeLs();
  const st = loadStore(ls);
  st.set('k', 'aaa');
  st.set('k', 'aaa');
  st.set('k', 'aaa');
  assert.equal(ls.calls.set, 1, 'identical string is written once');
  st.set('k', 'bbb');
  assert.equal(ls.calls.set, 2, 'changed string is written');
  assert.equal(ls.data.get('k'), 'bbb', 'latest value wins');
  st.put('j', { a: 1 });
  st.put('j', { a: 1 });
  assert.equal(ls.calls.set, 3, 'put with identical JSON is written once');
  st.put('j', { a: 2 });
  assert.equal(ls.calls.set, 4, 'put with changed JSON is written');
}

// исключение (квота): запись не помечается записанной и повторяется
{
  const failKeys = new Set(['k']);
  const ls = makeLs({ failKeys });
  const st = loadStore(ls);
  st.set('k', 'aaa');
  st.set('k', 'aaa');
  assert.equal(ls.calls.set, 2, 'failed write is not cached: retry goes to storage');
  failKeys.delete('k');
  st.set('k', 'aaa');
  assert.equal(ls.calls.set, 3, 'retry after quota frees up writes');
  st.set('k', 'aaa');
  assert.equal(ls.calls.set, 3, 'and is cached again after the successful write');
}

// del снимает пометку: после удаления та же строка пишется заново
{
  const ls = makeLs();
  const st = loadStore(ls);
  st.set('k', 'aaa');
  st.del('k');
  st.set('k', 'aaa');
  assert.equal(ls.calls.set, 2, 'del invalidates the written mark');
  assert.equal(ls.data.has('k'), true);
}

// режим памяти (sandbox без localStorage) пишет и не дублирует работу
{
  const st = loadStore(undefined);
  st.set('k', 'aaa');
  st.set('k', 'aaa');
  assert.equal(st.get('k'), 'aaa', 'memory mode keeps values');
}

// --- 2) saveNow: lux не парсится заново, побочные вызовы на месте ----------
const saveStart = html.indexOf('function saveNow() {');
assert.ok(saveStart >= 0, 'saveNow found');
const saveEnd = html.indexOf("\ndocument.addEventListener('visibilitychange'", saveStart);
assert.ok(saveEnd > saveStart, 'saveNow tail found');
{
  const saveSrc = html.slice(saveStart, saveEnd);
  assert.ok(!saveSrc.includes("store.json('shelter:lux'"), 'saveNow does not re-parse lux every time');
  assert.ok(saveSrc.includes('luxBase()') && saveSrc.includes('luxCommit(nextLux)'),
    'saveNow uses the in-memory lux cache');
  for (const needle of ['wpAfterTaskSave();', 'scheduleAchievementReconcile();',
                        "blobToStore('__chatsBlob', S.chats)",
                        'pwToStore(S.passwords)', 'flushVisits()']) {
    assert.ok(saveSrc.includes(needle), `side effect kept in saveNow: ${needle}`);
  }
  const luxStart = html.indexOf('function luxBase()');
  const luxEnd = html.indexOf('function saveNow() {', luxStart);
  const luxSrc = html.slice(luxStart, luxEnd);
  assert.match(luxSrc, /raw === _luxRaw/, 'lux cache invalidates on external key change');
  assert.match(luxSrc, /JSON\.parse\(raw\)/, 'lux falls back to parsing when the raw string changed');
}

// close/hidden сбрасывают отложенное сохранение и визиты
{
  const handlers = html.slice(saveEnd, html.indexOf('const persist = debounce', saveEnd) + 80);
  assert.ok(handlers.includes("document.addEventListener('visibilitychange'") &&
            handlers.includes('persist.flush()') && handlers.includes('flushVisits()'),
    'visibilitychange flushes the pending save together with visits');
  const unloadAt = html.indexOf("window.addEventListener('beforeunload'", saveEnd);
  const unloadLine = html.slice(unloadAt, html.indexOf('\n', unloadAt));
  assert.ok(unloadLine.includes('persist.flush()') && unloadLine.includes('flushVisits()'),
    'beforeunload flushes the pending save together with visits');
  assert.match(html, /g\.flush = \(\.\.\.a\) => \{[^}]*clearTimeout\(t\); t = null; f\(\.\.\.a\)/,
    'debounce exposes an immediate flush without changing its call form');
}

// --- замер: одинаковая последовательность сохранений ------------------------
// «До» (наивная запись каждого ключа всегда) против «после» (кэш store.set).
{
  const naive = makeLs();
  const cached = makeLs();
  const st = loadStore(cached);
  const payload = () => ({
    'shelter:theme': 'obsidian', 'shelter:ghost': '0', 'shelter:searchEngine': 'yandex',
    'shelter:spaces': '{"a":1}', 'shelter:currentSpace': 'main', 'shelter:activeTabId': 't1',
    'shelter:bookmarks': '[]', 'shelter:ext': '[]',
  });
  const run = (save, rows) => { for (let i = 0; i < rows; i++) for (const [k, v] of Object.entries(payload())) save(k, v); };
  run((k, v) => naive.setItem(k, v), 5);            // старое поведение: всегда пишем
  run((k, v) => st.set(k, v), 5);                   // новое: пропуск совпадений
  assert.equal(naive.calls.set, 40, 'old behaviour: 8 keys × 5 saves');
  assert.equal(cached.calls.set, 8, 'new behaviour: 8 keys written once');
  assert.deepEqual([...naive.data.entries()].sort(), [...cached.data.entries()].sort(),
    'storage bytes are identical for the same action sequence');
  console.log('setItem measurement: idle x5 saves 40 ->', cached.calls.set);
}

console.log('store write-cache and lux parse-cache regression passed');
