'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { webcrypto } = require('node:crypto');

const source = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'app-pin.js'), 'utf8');
const window = { crypto: webcrypto };
vm.runInNewContext(source, { window, TextEncoder, Uint8Array, ArrayBuffer, Number, Math, Object, String, RegExp });
const Pin = window.ShelterAppPin;
assert(Pin, 'the local PIN module must expose its small testable API');

(async () => {
  assert.equal(Pin.ITERATIONS, 600000, 'PIN checks use a deliberately expensive salted KDF');
  assert.equal(Pin.MIN_PIN_LENGTH, 4, 'new PINs require exactly four digits');
  assert.equal(Pin.MAX_PIN_LENGTH, 4, 'new PINs require exactly four digits');
  assert.equal(Pin.validatePin('5826'), true);
  assert.equal(Pin.validatePin('1234'), false, 'common ascending PINs are rejected');
  assert.equal(Pin.validatePin('4321'), false, 'common descending PINs are rejected');
  assert.equal(Pin.validatePin('7777'), false, 'repeated-digit PINs are rejected');
  assert.equal(Pin.validatePin('1212'), false, 'repeated patterns are rejected');
  assert.equal(Pin.validatePin('582'), false);
  assert.equal(Pin.validatePin('58260'), false, 'five digits are not accepted');
  assert.equal(Pin.validatePin('582604'), false, 'new PINs cannot retain the old six-digit length');
  assert.equal(Pin.validatePin('12a4'), false);
  assert.equal(Pin.validatePin('１２３４'), false, 'only ASCII digits are accepted');

  const record = await Pin.createRecord('5826', webcrypto);
  assert.equal(record.version, 2);
  assert.equal(record.pinLength, 4);
  assert.equal(record.algorithm, 'PBKDF2-SHA256');
  assert.equal(record.iterations, Pin.ITERATIONS);
  assert.match(record.salt, /^[0-9a-f]{32}$/);
  assert.match(record.verifier, /^[0-9a-f]{64}$/);
  assert.equal(Object.hasOwn(record, 'pin'), false, 'the PIN itself is never persisted');
  assert.equal(Object.hasOwn(record, 'masterKey'), false, 'the verifier is not a master-key input');
  assert.equal(await Pin.verifyPin('5826', record, webcrypto), true);
  assert.equal(await Pin.verifyPin('5827', record, webcrypto), false);
  assert.equal(await Pin.verifyPin('582604', record, webcrypto), false);
  await assert.rejects(Pin.createRecord('582604', webcrypto), /four digits/i,
    'new records reject six-digit PINs');

  // Existing v1 records used six-to-twelve digits. Keep those credentials
  // usable until the owner changes to a new four-digit PIN.
  const legacySalt = new Uint8Array(16).fill(0x5a);
  const legacyKey = await webcrypto.subtle.importKey(
    'raw', new TextEncoder().encode('582604'), 'PBKDF2', false, ['deriveBits']);
  const legacyBits = await webcrypto.subtle.deriveBits({
    name: 'PBKDF2', hash: 'SHA-256', salt: legacySalt, iterations: Pin.ITERATIONS
  }, legacyKey, 256);
  const hex = bytes => Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2, '0')).join('');
  const legacyRecord = {
    version: 1,
    algorithm: 'PBKDF2-SHA256',
    iterations: Pin.ITERATIONS,
    salt: hex(legacySalt),
    verifier: hex(legacyBits),
    failures: 0,
    lockedUntil: 0
  };
  assert.equal(Pin.normalizeRecord(legacyRecord).version, 1);
  assert.equal(Pin.validatePinForRecord('582604', legacyRecord), true);
  assert.equal(Pin.validatePinForRecord('5826', legacyRecord), false);
  assert.equal(await Pin.verifyPin('582604', legacyRecord, webcrypto), true);
  assert.equal(await Pin.verifyPin('582600', legacyRecord, webcrypto), false);
  const legacyFailure = Pin.recordFailure(legacyRecord, 100000);
  assert.equal(legacyFailure.version, 1, 'throttling preserves the legacy record format');
  assert.equal(Pin.clearFailures(legacyFailure).version, 1,
    'successful legacy recovery does not corrupt the old record before migration');
  assert.equal(Pin.validatePinForRecord('582604', record), false,
    'a new record never accepts a legacy-length PIN');

  assert.equal(Pin.normalizeRecord({ ...record, iterations: 2000000000 }), null,
    'untrusted storage cannot force an unbounded KDF cost');
  assert.equal(Pin.normalizeRecord({ ...record, pinLength: 6 }), null,
    'new records must declare the required four-digit length');
  assert.equal(Pin.normalizeRecord({ ...record, verifier: '00' }), null,
    'malformed records fail closed');

  let attempts = Pin.clearFailures(record);
  for (let i = 0; i < 4; i++) attempts = Pin.recordFailure(attempts, 100000);
  assert.equal(attempts.failures, 4);
  assert.equal(Pin.remainingLockMs(attempts, 100000), 0,
    'the first four attempts do not lock out a mistyped PIN');
  attempts = Pin.recordFailure(attempts, 100000);
  assert.equal(attempts.failures, 5);
  assert.equal(Pin.remainingLockMs(attempts, 100000), 30000,
    'the fifth attempt starts a persistent 30-second lockout');
  assert.equal(Pin.remainingLockMs(attempts, 130000), 0);
  attempts = Pin.recordFailure(attempts, 130000);
  attempts = Pin.recordFailure(attempts, 160000);
  attempts = Pin.recordFailure(attempts, 190000);
  assert.equal(Pin.remainingLockMs(attempts, 190000), 60000,
    'repeated failures after each wait increase the lockout delay');
  assert.equal(Pin.clearFailures(attempts).failures, 0);
  assert.equal(Pin.clearFailures(attempts).lockedUntil, 0);

  console.log('local four-digit PIN KDF, legacy compatibility, validation, and rate-limit tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
