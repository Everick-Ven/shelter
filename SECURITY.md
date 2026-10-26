# Reporting a security vulnerability

Please do not publish exploitable details, browser profiles, cookies, passwords, private keys, or unredacted logs in a public issue.

1. Prefer GitHub's **Report a vulnerability** / private vulnerability reporting flow for this repository, if GitHub shows it as available.
2. If that flow is unavailable, contact the repository owner privately through the GitHub account that owns `Everick-Ven/shelter` and ask for a secure disclosure channel. Do not send secrets or user data in a public issue or pull request.
3. Include the affected SHELTER version and operating system, a concise impact description, reproduction steps that use synthetic data, and any relevant sanitized logs. Do not attach a real browser profile or backup.

## Support status

A formal security-support window has not yet been published. Do not assume that an older release receives security fixes. When reporting an issue, test against the newest release only after verifying its detached minisign signature and SHA-256 manifest. The project currently builds CEF 154.0.32; the CEF maintenance status and known limits are tracked in `docs/cef-maintenance.md`.

## Release-integrity limits

- The detached minisign signature authenticates the release checksum manifest only when the signing public-key fingerprint has been independently trusted.
- GitHub build-provenance attestations describe the workflow provenance; they do not replace the minisign check or prove that the application is free of vulnerabilities.
- This project does not provide Windows Authenticode signing or macOS Developer ID signing/notarization. SmartScreen and Gatekeeper warnings may therefore appear and must not be treated as resolved by the checksum, signature, or attestation.
- Do not report a Gatekeeper or SmartScreen warning as a vulnerability by itself; report a concrete security impact or an unexpected change in the release artifacts.
