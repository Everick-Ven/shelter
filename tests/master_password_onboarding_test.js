'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = process.env.SHELTER_SOURCE_ROOT || path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const ui = read('resources/ui/index.html');
const bridge = read('resources/ui/host-bridge.js');
const scheme = read('src/ui_scheme.cc');
const key = read('src/secret_key.cc');

const setup = ui.slice(ui.indexOf('function masterPasswordNativeCall('),
  ui.indexOf('function openMasterPasswordUnlock('));
const init = ui.slice(ui.indexOf('function init() {'), ui.indexOf('/* =========================================================\n   SHELTER · Планировщик'));
const load = ui.slice(ui.indexOf('function load() {'), ui.indexOf('function saveNow() {'));
const save = ui.slice(ui.indexOf('function saveNow() {'), ui.indexOf("\ndocument.addEventListener('visibilitychange'"));
const passwordAndPinFlow = ui.slice(ui.indexOf('function masterPasswordNativeCall('), ui.indexOf('function settingsHtml() {'));

assert(setup.includes('dismiss: !required'), 'required first-run setup must not be dismissible');
assert(setup.includes('Задать пароль и продолжить'), 'first-run flow must require an explicit password setup');
assert(setup.includes("masterPasswordNativeCall('secretPasswordSet', phrase)"), 'setup must create the native recovery wrapper');
assert(!setup.includes("masterPasswordNativeCall('secretPasswordUnlock', phrase)"),
  'the setup flow and unlock flow remain separate');
assert(init.includes('S.masterKeyPasswordStatusKnown &&') &&
  init.includes('!S.masterKeyPasswordAvailable && masterPasswordDataReadable()'),
  'readable profiles without a wrapper must be gated before normal startup');
assert(init.includes('if (passwordStatusNeedsRetry) openMasterPasswordStatusRetry()'),
  'status failures must be retried rather than mistaken for an absent wrapper');
assert(init.includes('if (!mandatoryPasswordSetup && !passwordStatusNeedsRetry)'),
  'automatic writes must wait until required setup and status checks finish');
assert(load.includes('S.masterKeyPasswordStatusKnown = true') &&
  load.includes('S.masterKeyPasswordAvailable = passwordStatus'),
  'status errors must remain distinguishable from an absent wrapper');
assert(/store\.(?:put|set)\(\s*['"]shelter:board['"]/.test(save) &&
  /store\.(?:put|set)\(\s*['"]shelter:boards['"]/.test(save),
  'normal board persistence remains on its existing saveNow path');
assert(!/(?:store\.(?:put|set)\(\s*['"]shelter:(?:board|boards)['"]|wpPersistBoard\s*\(|saveNow\s*\()/.test(passwordAndPinFlow),
  'master-password and PIN flows do not directly rewrite board records');
assert(!passwordAndPinFlow.includes('wpStoreEncrypt(') && !passwordAndPinFlow.includes('wpStoreParse('),
  'master-password setup and recovery do not alter the existing board codec');
assert(ui.includes("row('Парольная фраза мастер-ключа'"),
  'Settings must expose password setup/change');
assert(ui.includes('data-set="master-password-recovery"') &&
  ui.includes("else if (a === 'master-password-recovery') openMasterPasswordUnlock();"),
  'master-key recovery has a Settings entry point independent of task-store errors');
assert(!ui.includes('data-act="masterPasswordUnlock"'),
  'master-key recovery must not depend on the task-store error view');
assert(!passwordAndPinFlow.includes('persist.flush'),
  'master-password setup and recovery do not rely on a debounce flush to preserve the board');
assert(setup.includes('Array.from(phrase).length < 10'),
  'master-password creation requires at least ten Unicode code points');
assert(setup.includes('не менее 10 символов'),
  'the creation error message matches the ten-character minimum');
assert(setup.includes('appPinOfferSetup()'),
  'completing mandatory master-password setup offers the optional local PIN shortcut');

assert(!ui.includes('master-password-remove'), 'the mandatory password wrapper cannot be removed in UI');
assert(key.includes('LoadKeyLocked(true, error)'), 'fresh setup may create only the first master key');
assert(key.includes('g_key_creation_blocked'), 'a prior decrypt failure must prevent replacement-key creation');
assert(bridge.includes('if (xhr.status !== 200 || !response || response.ok !== true) return null;'),
  'native status failure must not look like an absent wrapper');
assert(!bridge.includes('secretPasswordRemove'), 'the UI bridge must not remove the mandatory wrapper');
assert(scheme.includes('action == "status" || action == "set" || action == "unlock"'),
  'only status, setup, and recovery routes are exposed');
assert(!scheme.includes('password-remove'), 'the native UI route must not remove the mandatory wrapper');

console.log('master-password onboarding, separate recovery entry, and unchanged board persistence tests passed');
