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
  assert.equal(Pin.validatePin('582604'), true);
  assert.equal(Pin.validatePin('58260417'), true);
  assert.equal(Pin.validatePin('123456'), false, 'common ascending PINs are rejected');
  assert.equal(Pin.validatePin('654321'), false, 'common descending PINs are rejected');
  assert.equal(Pin.validatePin('777777'), false, 'repeated-digit PINs are rejected');
  assert.equal(Pin.validatePin('12345'), false);
  assert.equal(Pin.validatePin('12a456'), false);
  assert.equal(Pin.validatePin('１２３４５６'), false, 'only ASCII digits are accepted');

  const record = await Pin.createRecord('582604', webcrypto);
  assert.equal(record.version, 1);
  assert.equal(record.algorithm, 'PBKDF2-SHA256');
  assert.equal(record.iterations, Pin.ITERATIONS);
  assert.match(record.salt, /^[0-9a-f]{32}$/);
  assert.match(record.verifier, /^[0-9a-f]{64}$/);
  assert.equal(Object.hasOwn(record, 'pin'), false, 'the PIN itself is never persisted');
  assert.equal(Object.hasOwn(record, 'masterKey'), false, 'the verifier is not a master-key input');
  assert.equal(await Pin.verifyPin('582604', record, webcrypto), true);
  assert.equal(await Pin.verifyPin('582605', record, webcrypto), false);

  assert.equal(Pin.normalizeRecord({ ...record, iterations: 2000000000 }), null,
    'untrusted storage cannot force an unbounded KDF cost');
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

  console.log('local PIN KDF, validation, and rate-limit tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
