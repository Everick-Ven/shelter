'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const bridge = read('resources/ui/host-bridge.js');
const html = read('resources/ui/index.html');
const scheme = read('src/ui_scheme.cc');
const native = read('src/shell_bridge.cc');
const shell = read('src/shell.cc');
const cmake = read('CMakeLists.txt');

assert.match(cmake, /option\(SHELTER_CI_HOOKS[^\n]*OFF\)/,
  'diagnostic hooks must be disabled unless explicitly enabled for CI');
assert.match(scheme,
  /#if defined\(SHELTER_CI_HOOKS\)[\s\S]*ReplaceAll\(&data, "__SHELTER_CI_HOOKS__", "true"\)[\s\S]*#else[\s\S]*ReplaceAll\(&data, "__SHELTER_CI_HOOKS__", "false"\)[\s\S]*#endif/,
  'the runtime must receive an explicit diagnostic-build flag');
assert.match(bridge, /var DIAGNOSTICS_ENABLED = '__SHELTER_CI_HOOKS__' === 'true';/);
assert.match(bridge, /setTimeout\(flushPerf, 6000\)/,
  'UI diagnostic summaries must be rate-limited');
assert.match(bridge, /samples\.push\(\{ method: name, count: bucket\.count, totalMs: bucket\.totalMs, maxMs: bucket\.maxMs \}\)/,
  'UI summaries must contain only a method label and numeric measurements');
assert.match(bridge, /request: JSON\.stringify\(\{ m: 'diag\.performance', a: \{ samples: samples \} \}\)/,
  'performance data must use the authorized native bridge method');
assert.match(html, /entry\.duration < 100/,
  'the long-task observer must ignore tasks shorter than 100 ms');
assert.match(html, /window\.__shRecordPerf\('ui\.longtask', entry\.duration\)/,
  'long tasks must be aggregated without recording entry details');
for (const name of ['freeze.snapshot', 'freeze.decode', 'freeze.hide', 'freeze.total', 'thaw.show', 'thaw.total']) {
  assert.ok(bridge.includes(`recordPerf('${name}'`), `missing ${name} timing`);
}

const namesStart = bridge.indexOf('  var DIAGNOSTICS_ENABLED =');
const namesEnd = bridge.indexOf('\n  /* ---------- cefQuery ---------- */', namesStart);
assert.ok(namesStart >= 0 && namesEnd > namesStart, 'the safe cefQuery instrumentation block must exist');
const helperSource = bridge.slice(namesStart, namesEnd)
  .replace("'__SHELTER_CI_HOOKS__'", "'true'");
let clock = 10;
const metrics = [];
const fakeWindow = { cefQuery: null };
const helpers = new Function('window', 'performance', 'recordPerf',
  `${helperSource}\nreturn { DIAGNOSTICS_ENABLED, safePerfMethod, perfMethodFromOptions, installCefQueryMeasurement };`)(
  fakeWindow,
  { now: () => clock },
  (method, duration) => metrics.push([method, duration])
);
assert.equal(helpers.DIAGNOSTICS_ENABLED, true);
assert.equal(helpers.perfMethodFromOptions({ request: JSON.stringify({ m: 'view.open', a: { url: 'https://private.invalid', title: 'private task' } }) }), 'view.open');
assert.equal(helpers.perfMethodFromOptions({ request: JSON.stringify({ cmd: 'window:minimize', secret: 'private task' }) }), 'window.minimize');
assert.equal(helpers.perfMethodFromOptions({ request: JSON.stringify({ m: 'https://private.invalid/path', a: {} }) }), 'other');
assert.equal(helpers.safePerfMethod('private task contents'), 'other');

let forwardedOptions = null;
let successValue = '';
const original = function (options) {
  forwardedOptions = options;
  clock = 42;
  options.onSuccess('ok');
  return 7;
};
const measured = helpers.installCefQueryMeasurement(original);
const request = JSON.stringify({ m: 'view.open', a: { url: 'https://private.invalid', title: 'private task' } });
assert.equal(measured.call(fakeWindow, { request, onSuccess: value => { successValue = value; } }), 7);
assert.equal(successValue, 'ok');
assert.equal(forwardedOptions.request, request, 'instrumentation must leave the bridge request intact');
assert.deepEqual(metrics, [['view.open', 32]], 'only the allow-listed method and elapsed milliseconds reach the recorder');
assert.equal(JSON.stringify(metrics).includes('private.invalid'), false);
assert.equal(JSON.stringify(metrics).includes('private task'), false);

const disabledSource = bridge.slice(namesStart, namesEnd)
  .replace("'__SHELTER_CI_HOOKS__'", "'false'");
const disabled = new Function('window', 'performance', 'recordPerf',
  `${disabledSource}\nreturn { DIAGNOSTICS_ENABLED, installCefQueryMeasurement };`)(
  fakeWindow, { now: () => clock }, () => assert.fail('release builds must not record performance data'));
assert.equal(disabled.DIAGNOSTICS_ENABLED, false);
assert.equal(disabled.installCefQueryMeasurement(original), original,
  'release builds must leave cefQuery unwrapped');

const handleStart = native.indexOf('bool Shell::HandleBridge(');
const handleEnd = native.indexOf('// ---- действия над вкладкой', handleStart);
const handle = native.slice(handleStart, handleEnd);
assert.ok(handleStart >= 0 && handleEnd > handleStart);
assert.match(handle, /BridgePerfClock::now\(\)/);
assert.match(handle, /DiagnosticMethod\(m\)/);
assert.match(handle, /elapsed_ms >= 100\.0/);
assert.match(handle, /Log\(line\.str\(\)\)/);
const uiDiagAt = handle.indexOf('if (m == "diag.performance")');
assert.ok(uiDiagAt > 0, 'native UI diagnostics handler must be implemented');
assert.ok(handle.lastIndexOf('#if defined(SHELTER_CI_HOOKS)', uiDiagAt) >= 0,
  'the native diagnostics method must be compiled only for CI hooks');
assert.match(handle.slice(uiDiagAt), /DiagnosticMethodNames\(\)\.count\(method\)/,
  'native logging must reject unrecognized sample labels');
assert.match(handle.slice(uiDiagAt), /now - last_ui_report < std::chrono::seconds\(5\)/,
  'native UI summaries must be rate-limited');
assert.match(shell, /kMaxLogBytes = 512 \* 1024/);
assert.match(shell, /shelter\.log\.1/,
  'the existing single-backup log rotation must remain active');

console.log('CI-gated, privacy-safe performance diagnostics tests passed');
