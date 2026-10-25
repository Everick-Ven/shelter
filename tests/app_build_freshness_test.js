'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'resources/ui/index.html'), 'utf8');
const bridge = fs.readFileSync(path.join(root, 'resources/ui/host-bridge.js'), 'utf8');
const scheme = fs.readFileSync(path.join(root, 'src/ui_scheme.cc'), 'utf8');
const versionHeader = fs.readFileSync(path.join(root, 'cmake/app_version.h.in'), 'utf8');

const start = html.indexOf('const BUILD_STALE_AFTER_DAYS = 90;');
const end = html.indexOf('\n}', html.indexOf('function shelterBuildIsStale(', start)) + 2;
assert(start >= 0 && end > start, 'offline build freshness policy must be present');
const policy = html.slice(start, end);
assert(!/fetch\s*\(|XMLHttpRequest|cefQuery|shelterNative\./.test(policy),
       'staleness warning must not make network or native bridge calls');
const context = {};
vm.runInNewContext(policy + '\nglobalThis.check = shelterBuildIsStale;', context);
const check = context.check;
const builtAt = Date.UTC(2026, 0, 1, 12, 30, 0);
const stamp = '20260101123000';
const day = 24 * 60 * 60 * 1000;
assert.strictEqual(check(stamp, builtAt + 89 * day), false, 'a build younger than 90 days is not stale');
assert.strictEqual(check(stamp, builtAt + 90 * day), true, 'a build at 90 days is stale');
assert.strictEqual(check(stamp, builtAt - 1), false, 'a future-dated build is not called stale');
assert.strictEqual(check('20260230000000', builtAt + 200 * day), false, 'an impossible date is ignored');
assert.strictEqual(check('not-a-timestamp', builtAt + 200 * day), false, 'a missing/invalid build timestamp is ignored');
assert.match(html, /shelterBuildIsStale\(BUILD_TIMESTAMP, Date\.now\(\)\)/);
assert.match(html, /Сборка старше 90 дней/);
assert.match(bridge, /buildTimestamp: BUILD_TIMESTAMP/);
assert.match(scheme, /ReplaceAll\(&data, "__APP_BUILD_TIMESTAMP__", kAppBuildTimestamp\)/);
assert.match(versionHeader, /kAppBuildTimestamp\[\] = "@SHELTER_BUILD_TIMESTAMP@"/);

console.log('offline build freshness warning regression test passed');
