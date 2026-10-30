/*
 * Local app-lock PIN verifier. This is deliberately separate from the
 * master-key/password format and is never used to encrypt or unwrap a key.
 */
(function (global) {
  'use strict';

  const ITERATIONS = 600000;
  const LEGACY_VERSION = 1;
  const VERSION = 2;
  const PIN_LENGTH = 4;
  const ALGORITHM = 'PBKDF2-SHA256';
  const SALT_BYTES = 16;
  const VERIFIER_BYTES = 32;
  const MAX_LOCKOUT_MS = 15 * 60 * 1000;

  function validatePinLength(pin, min, max) {
    if (typeof pin !== 'string' || !new RegExp('^\\d{' + min + ',' + max + '}$').test(pin)) return false;
    if (/^(\d)\1+$/.test(pin)) return false;

    const digits = Array.from(pin, ch => ch.charCodeAt(0) - 48);
    const ascending = digits.every((n, i) => i === 0 || n === (digits[i - 1] + 1) % 10);
    const descending = digits.every((n, i) => i === 0 || n === (digits[i - 1] + 9) % 10);
    if (ascending || descending) return false;

    for (let period = 1; period <= Math.floor(pin.length / 2); period++) {
      if (pin.length % period !== 0) continue;
      const pattern = pin.slice(0, period);
      if (pattern.repeat(pin.length / period) === pin) return false;
    }
    return true;
  }

  function validatePin(pin) {
    return validatePinLength(pin, PIN_LENGTH, PIN_LENGTH);
  }

  // v1 PINs were 6–12 digits. Keep them usable during migration; all newly
  // created or changed PINs use the exact four-digit v2 format.
  function validateLegacyPin(pin) {
    return validatePinLength(pin, 6, 12);
  }

  function bytesToHex(bytes) {
    let out = '';
    for (let i = 0; i < bytes.length; i++) out += bytes[i].toString(16).padStart(2, '0');
    return out;
  }

  function hexToBytes(hex) {
    const out = new Uint8Array(hex.length / 2);
    for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    return out;
  }

  function constantTimeHexEqual(left, right) {
    const a = typeof left === 'string' ? left : '';
    const b = typeof right === 'string' ? right : '';
    let diff = a.length ^ b.length;
    const length = Math.max(a.length, b.length);
    for (let i = 0; i < length; i++) {
      diff |= (i < a.length ? a.charCodeAt(i) : 0) ^ (i < b.length ? b.charCodeAt(i) : 0);
    }
    return diff === 0;
  }

  function cryptoApi(provider) {
    const api = provider || global.crypto;
    if (!api || typeof api.getRandomValues !== 'function' || !api.subtle ||
        typeof api.subtle.importKey !== 'function' || typeof api.subtle.deriveBits !== 'function') {
      throw new Error('Web Crypto is required for local PIN locking.');
    }
    return api;
  }

  async function deriveVerifier(pin, salt, provider) {
    const api = cryptoApi(provider);
    const encoder = new TextEncoder();
    const key = await api.subtle.importKey('raw', encoder.encode(pin), 'PBKDF2', false, ['deriveBits']);
    const bits = await api.subtle.deriveBits({
      name: 'PBKDF2',
      hash: 'SHA-256',
      salt,
      iterations: ITERATIONS
    }, key, VERIFIER_BYTES * 8);
    return bytesToHex(new Uint8Array(bits));
  }

  function normalizeRecord(value, now) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
    const legacy = value.version === LEGACY_VERSION && value.pinLength === undefined;
    const current = value.version === VERSION && value.pinLength === PIN_LENGTH;
    if ((!legacy && !current) || value.algorithm !== ALGORITHM || value.iterations !== ITERATIONS) return null;
    if (typeof value.salt !== 'string' || !/^[0-9a-f]{32}$/.test(value.salt)) return null;
    if (typeof value.verifier !== 'string' || !/^[0-9a-f]{64}$/.test(value.verifier)) return null;
    if (!Number.isSafeInteger(value.failures) || value.failures < 0 || value.failures > 1000000) return null;
    if (!Number.isFinite(value.lockedUntil) || value.lockedUntil < 0) return null;

    const timestamp = Number.isFinite(now) ? now : Date.now();
    const cap = timestamp + MAX_LOCKOUT_MS;
    const record = {
      version: value.version,
      algorithm: ALGORITHM,
      iterations: ITERATIONS,
      salt: value.salt,
      verifier: value.verifier,
      failures: value.failures,
      lockedUntil: Math.min(value.lockedUntil, cap)
    };
    if (current) record.pinLength = PIN_LENGTH;
    return record;
  }

  function validatePinForRecord(pin, record) {
    const normalized = normalizeRecord(record);
    if (!normalized) return false;
    return normalized.version === LEGACY_VERSION ? validateLegacyPin(pin) : validatePin(pin);
  }

  async function createRecord(pin, provider) {
    if (!validatePin(pin)) throw new Error('Use a non-obvious PIN of exactly four digits.');
    const api = cryptoApi(provider);
    const salt = api.getRandomValues(new Uint8Array(SALT_BYTES));
    const verifier = await deriveVerifier(pin, salt, api);
    return {
      version: VERSION,
      pinLength: PIN_LENGTH,
      algorithm: ALGORITHM,
      iterations: ITERATIONS,
      salt: bytesToHex(salt),
      verifier,
      failures: 0,
      lockedUntil: 0
    };
  }

  function remainingLockMs(record, now) {
    const normalized = normalizeRecord(record, now);
    if (!normalized) return 0;
    return Math.max(0, normalized.lockedUntil - (Number.isFinite(now) ? now : Date.now()));
  }

  function lockoutDelayMs(failures) {
    if (failures < 5) return 0;
    const exponent = Math.min(10, Math.floor((failures - 5) / 3));
    return Math.min(MAX_LOCKOUT_MS, 30000 * (2 ** exponent));
  }

  function recordFailure(record, now) {
    const timestamp = Number.isFinite(now) ? now : Date.now();
    const normalized = normalizeRecord(record, timestamp);
    if (!normalized) throw new Error('Invalid local PIN record.');
    if (remainingLockMs(normalized, timestamp) > 0) return normalized;
    const failures = Math.min(1000000, normalized.failures + 1);
    return Object.assign({}, normalized, {
      failures,
      lockedUntil: timestamp + lockoutDelayMs(failures)
    });
  }

  function clearFailures(record, now) {
    const normalized = normalizeRecord(record, now);
    if (!normalized) throw new Error('Invalid local PIN record.');
    return Object.assign({}, normalized, { failures: 0, lockedUntil: 0 });
  }

  async function verifyPin(pin, record, provider) {
    const normalized = normalizeRecord(record);
    if (!normalized || !validatePinForRecord(pin, normalized) || remainingLockMs(normalized) > 0) return false;
    const actual = await deriveVerifier(pin, hexToBytes(normalized.salt), provider);
    return constantTimeHexEqual(actual, normalized.verifier);
  }

  global.ShelterAppPin = Object.freeze({
    ITERATIONS,
    MIN_PIN_LENGTH: PIN_LENGTH,
    MAX_PIN_LENGTH: PIN_LENGTH,
    validatePin,
    validatePinForRecord,
    normalizeRecord,
    createRecord,
    verifyPin,
    recordFailure,
    clearFailures,
    remainingLockMs
  });
})(window);
