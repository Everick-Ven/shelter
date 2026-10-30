'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const ui = fs.readFileSync(path.join(root, 'resources/ui/index.html'), 'utf8');
const pinModule = fs.readFileSync(path.join(root, 'resources/ui/app-pin.js'), 'utf8');

assert.match(ui, /<script\s+src="app-pin\.js"><\/script>/,
  'the isolated PIN verifier is loaded as an external same-origin script');
assert.match(pinModule, /PBKDF2/);
assert.match(pinModule, /600000/);
assert.doesNotMatch(pinModule, /secretEnc|secretDec|secretPassword(?:Set|Unlock)|secret\.key/i,
  'the PIN module never encrypts, decrypts, sets, or unwraps the master key');

const pinUiStart = ui.indexOf('function appPinStorageAvailable()');
const pinUiEnd = ui.indexOf('function settingsHtml()', pinUiStart);
assert(pinUiStart >= 0 && pinUiEnd > pinUiStart, 'PIN UI implementation is present as a separate section');
const pinUi = ui.slice(pinUiStart, pinUiEnd);
assert.match(pinUi, /function appPinCanConfigure\(\)[\s\S]*?S\.masterKeyPasswordAvailable && masterPasswordCanConfigure\(\)/,
  'PIN setup requires readable existing data and the separate master-password wrapper');
assert.match(pinUi, /masterPasswordNativeCall\('secretPasswordUnlock'/,
  'forgotten-PIN recovery validates the existing master-password wrapper');
assert.match(pinUi, /appPinRemoveRecord\(\)/,
  'master-password fallback resets only the local PIN record');
assert.doesNotMatch(pinUi, /secretPasswordSet|secretEnc\(|secretDec\(|shelter:board/,
  'PIN setup does not create a key, re-encrypt the board, or change ss1 storage');
assert.match(pinUi, /app\.inert = true[\s\S]*?hideNativeView\(/,
  'locking makes the UI inert and hides the native web-content view');
assert.match(ui, /appPinLoad\(\);\s*const pinRecoveryBypass = appPinConsumeRecoveryBypass\(\)/);
assert.match(ui, /const shouldStartPinLocked = APP_PIN\.configured && !pinRecoveryBypass && !passwordStatusNeedsRetry && !mandatoryPasswordSetup;\s*if \(shouldStartPinLocked\) appPinLockNow\('startup'\);\s*else renderView\(true\)/,
  'a configured PIN locks before the native web view is first shown, after master-password onboarding checks');
assert.match(ui, /row\('Локальная блокировка по PIN'/,
  'the PIN lock has a settings entry');
assert.match(ui, /data-set="master-password-recovery"[\s\S]*?Восстановить доступ/,
  'master-password recovery is reachable from Settings, independent of task-store errors');
assert.match(ui, /else if \(a === 'master-password-recovery'\) openMasterPasswordUnlock\(\)/,
  'the Settings recovery entry opens the master-key unlock flow');
assert.doesNotMatch(pinUi, /persist\.flush\s*\(/,
  'PIN setup and recovery never flush or rewrite board persistence');

console.log('local PIN lock UI integration and master-key separation tests passed');
