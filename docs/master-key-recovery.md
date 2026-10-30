# Master-key recovery and password wrapper

**Implementation note (2026-10-30).** This describes the current source changes; macOS runtime behavior on the affected device is not yet confirmed.

## What the password does

SHELTER continues to encrypt stored secrets with the existing random 256-bit master key and the existing `ss1:` format. A long password phrase is required during first-run setup for a clean profile and for an existing profile whose encrypted data can be read safely. The phrase does **not** replace or re-key the master key. It derives a key with PBKDF2-HMAC-SHA256 (600,000 iterations by default) and stores an authenticated `ss1:` encryption of the existing master key in `secret.key.password` as:

```text
skw1:<iterations>:<salt-hex>:<ss1-wrapped-key>
```

The wrapper stores neither the password nor the plaintext master key. The reader enforces a minimum production KDF cost and a bounded maximum. A long, unique passphrase is recommended; the UI requires at least 12 Unicode characters. A short PIN is not recommended as a replacement for this phrase.

## Separate local app PIN lock

SHELTER supports a separate local app-lock PIN. It locks the renderer UI at startup and on demand; it does not replace the master-password phrase, decrypt data, or protect/unwrap the master key. The existing `ss1:` format and board persistence are unchanged by PIN or master-password setup/recovery.

The profile stores a random 16-byte salt and a PBKDF2-HMAC-SHA256 verifier (600,000 iterations), not the PIN itself. The UI accepts 6–12 ASCII digits, rejects obvious repeated/sequential patterns, and persists failed-attempt throttling (30 seconds after the fifth failure, increasing to a 15-minute cap). If the PIN is forgotten, entering the existing master-password phrase resets only the local PIN record; it does not delete or replace the master key, password wrapper, board records, or other user data.

This is deliberately a convenience lock, not a data-at-rest security boundary. A person who can edit the local profile can remove or alter the PIN record, and the low-entropy PIN verifier can be guessed offline despite the KDF. Use the long, unique master-password phrase for recovery; never use the PIN to protect or recover the master key.

## Important limitation

For a clean profile, first-run setup creates one new random master key and immediately wraps it. For an existing profile, setup is offered only when the current encrypted data is readable with its existing key. If the key was already lost before the wrapper was created, choosing a new password cannot reconstruct it or decrypt existing `ss1:` records. After a decrypt error, SHELTER keeps the affected data read-only and does not force setup or create a replacement key.

Master-password recovery is available from Settings whenever a password wrapper is present; it is not dependent on the task-store error screen. A successful password unwrap reloads the data using the recovered existing key. It does not rewrite or re-encrypt task-board records. If decryption still fails, the app keeps affected data read-only.

The wrapper is mandatory in the app UI and cannot be removed there; it remains an additional recovery copy without changing the master key, `ss1:` records, or task-board persistence. Existing key files and Keychain items are not deleted by setup or unlock.

## Safe diagnosis on macOS

1. Do not delete the SHELTER profile, `secret.key`, Keychain items, or reinstall the app before preserving the profile.
2. Close SHELTER and make a private backup copy of the full user-data directory. It may contain sensitive data and keys: keep it encrypted/private and do not send the copy to anyone.
3. Inspect `~/Library/Application Support/SHELTER/keychain.log`. The relevant diagnostic markers are `secret-key: master key load failed`, `decrypt failed at master-key`, and `decrypt failed at ss1-core`. They distinguish inability to obtain a key from failure to decrypt/parse with a key; neither one alone proves that the master key is permanently lost.
4. If asking for support, share only the relevant log lines and redact unrelated paths/account names. Never send `secret.key`, the password wrapper, passwords, or board/task contents.

## Native verification

`tests/secret_key_recovery_test.cc` exercises a missing-key decrypt path (no replacement key is created), clean first-run key creation and wrapping, setup around an already-used key, unlocking with the original key, and decryption of an unchanged `ss1` sample. It uses temporary test directories only. Separate macOS/Windows packaged-runtime acceptance is still required.
