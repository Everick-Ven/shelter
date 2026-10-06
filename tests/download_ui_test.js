'use strict';

// Regression coverage for the download UI model without launching CEF.
// The functions under test are extracted from the inline application script so
// the test exercises the same implementation that is shipped in index.html.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');
const helperStart = html.indexOf('const fmtSize =');
const helperEnd = html.indexOf('const dlRows =', helperStart);
const eventStart = html.indexOf('function dlFilenameFromPath(path)');
const eventEnd = html.indexOf('\nconst pwFor =', eventStart);
assert.ok(helperStart >= 0 && helperEnd > helperStart, 'download display helpers are present');
assert.ok(eventStart >= 0 && eventEnd > eventStart, 'download event handler is present');

const context = {
  S: { downloads: [], prefs: { askDownload: false }, ui: {} },
  window: {},
  uid: (() => { let n = 0; return prefix => `${prefix}-test-${++n}`; })(),
  persist() {},
  renderBadges() {},
  $(selector) { return null; },
  dlPanelRefresh() { return false; },
  dlPanelDone() {},
  internalPage() { return null; },
  curTab() { return null; },
  toast() {},
  dlRows() { return ''; },
  ico() { return ''; },
  esc(value) { return String(value); },
  relDay() { return 'сегодня'; },
  hm() { return '12:00'; },
};
vm.createContext(context);
vm.runInContext(
  html.slice(helperStart, helperEnd) + '\n' +
  html.slice(eventStart, eventEnd) + '\n' +
  'globalThis.__downloadTest = { dlRow, dlProgressBar, dlProgressText, dlPercent, dlTotalBytes, dlReceivedBytes, dlNativeControl, normalizeDownloadHistory };',
  context,
  { filename: 'resources/ui/index.html' }
);

const promptStart = html.indexOf("    cls: 'dl-ask'");
const promptEnd = html.indexOf('\n  });', promptStart);
assert.ok(promptStart >= 0 && promptEnd > promptStart, 'download prompt markup is present');
const promptMarkup = html.slice(promptStart, promptEnd);
const promptActions = Array.from(promptMarkup.matchAll(/data-dlp="([^"]+)"/g), match => match[1]);
assert.deepEqual(promptActions, ['cancel', 'save'], 'prompt has only Cancel and Download actions');
assert.match(promptMarkup, /data-dlp="cancel">Отмена/);
assert.match(promptMarkup, /data-dlp="save">Скачать/);
assert.doesNotMatch(promptMarkup, /saveAs|Сохранить как/);

const restored = context.__downloadTest.normalizeDownloadHistory([
  { id: 'demo', state: 'run', p: 64, demo: true },
  { id: 'stale-run', state: 'run', p: 42, speedBytesPerSecond: 99, totalBytes: 1024 },
  { id: 'stale-pause', state: 'pause', p: 15, speedBytesPerSecond: 0 },
  { id: 'finished', state: 'done', totalBytes: 1024, demo: false },
]);
assert.equal(restored.length, 3, 'demo downloads are not restored as real history');
assert.equal(restored.find(download => download.id === 'stale-run').state, 'interrupted');
assert.equal(restored.find(download => download.id === 'stale-run').p, null);
assert.equal(restored.find(download => download.id === 'stale-run').speedBytesPerSecond, 0);
assert.equal(restored.find(download => download.id === 'finished').state, 'done');
assert.equal(context.__downloadTest.normalizeDownloadHistory(null).length, 0);

const emit = context.window.shelterDownload;
assert.equal(typeof emit, 'function');

// Unknown/negative CEF totals must not become a negative size or a fake 0% bar.
emit({
  id: 'dl-unknown', filename: 'stream.bin', state: 'progressing',
  bytes: 512, total: -1, percent: -1, speed: 64,
});
let unknown = context.S.downloads.find(download => download.mid === 'dl-unknown');
assert.ok(unknown, 'unknown-length download is tracked');
assert.equal(unknown.totalBytes, 0);
assert.equal(unknown.size, 0);
assert.equal(unknown.p, null);
assert.equal(unknown.bytesReceived, 512);
let row = context.__downloadTest.dlRow(unknown, 0);
assert.match(row, /dl-indeterminate/);
assert.doesNotMatch(row, /aria-valuenow=/);
assert.match(row, /Размер файла неизвестен/);
assert.match(row, /512 Б · размер неизвестен/);

// When CEF has a content length, bytes, percent, total and speed stay aligned.
emit({
  id: 'dl-known', filename: 'archive.zip', state: 'progressing',
  bytes: 1048576, total: 4194304, percent: 25, speed: 131072,
});
let known = context.S.downloads.find(download => download.mid === 'dl-known');
assert.equal(known.totalBytes, 4194304);
assert.equal(known.bytesReceived, 1048576);
assert.equal(known.p, 25);
assert.equal(known.size, 4);
row = context.__downloadTest.dlRow(known, 0);
assert.match(row, /width:25%/);
assert.doesNotMatch(row, /dl-indeterminate/);
assert.match(row, /25% · 1,0 МБ из 4,0 МБ/);
assert.match(row, /128 КБ\/с/);

// Pause is a real native command, and its status is represented separately.
context.window.shelterNative = {
  downloadControl(request) {
    context.__lastDownloadControl = request;
    return Promise.resolve({ ok: true });
  },
};
(async () => {
  context.__downloadTest.dlNativeControl(known, 'pause');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(context.__lastDownloadControl.id, 'dl-known');
  assert.equal(context.__lastDownloadControl.action, 'pause');
  assert.equal(known.state, 'pause');
  assert.match(context.__downloadTest.dlRow(known, 0), /dl-paused/);

  emit({
    id: 'dl-known', filename: 'archive.zip', state: 'completed',
    bytes: 0, total: 4194304, percent: 100, speed: 0,
    path: '/tmp/archive.zip',
  });
  known = context.S.downloads.find(download => download.mid === 'dl-known');
  assert.equal(known.state, 'done');
  assert.equal(known.bytesReceived, 4194304);
  assert.equal(known.totalBytes, 4194304);
  assert.equal(known.path, '/tmp/archive.zip');

  emit({ id: 'dl-unknown', filename: 'stream.bin', state: 'cancelled' });
  assert.equal(context.S.downloads.some(download => download.mid === 'dl-unknown'), false);
  console.log('DOWNLOAD_UI_TEST_PASS');
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
