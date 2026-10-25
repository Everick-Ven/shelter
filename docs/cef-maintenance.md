# CEF pin, freshness review, and offline age warning

## Audited state (2026-10-25)

| Item | Value |
|---|---|
| Project CEF pin | `154.0.32+g682c378+chromium-154.0.8037.58`, minimal distribution |
| Current stable shown by the official CEF build index | `156.0.14+g4fa4ab5+chromium-156.0.8078.48` |
| Drift | The project is two Chromium major numbers behind the stable index target. This identifies maintenance lag; it is **not** a CVE count or proof that a particular vulnerability is present/fixed. |
| Last source check | 2026-10-25; official index below. No Windows/macOS CEF upgrade build was performed for this audit. |

The CEF index snapshot identifies the version above under **Current Stable Build (Preferred)**: <https://cef-builds.spotifycdn.com/index.html>. The project's pinned archive names and hashes are below. The official index primarily lists SHA-1 links; the CEF distribution also serves the following `.sha256` sidecars, which were read directly on 2026-10-25:

| Platform archive (minimal) | Official SHA-256 sidecar | Pinned SHA-256 |
|---|---|---|
| `cef_binary_154.0.32+g682c378+chromium-154.0.8037.58_windows64_minimal.tar.bz2` | <https://cef-builds.spotifycdn.com/cef_binary_154.0.32%2Bg682c378%2Bchromium-154.0.8037.58_windows64_minimal.tar.bz2.sha256> | `aa1f7ab28005307edcc13f95e3ce5cd9d221894b00458fb5e368c00603204a2a` |
| `cef_binary_154.0.32+g682c378+chromium-154.0.8037.58_macosx64_minimal.tar.bz2` | <https://cef-builds.spotifycdn.com/cef_binary_154.0.32%2Bg682c378%2Bchromium-154.0.8037.58_macosx64_minimal.tar.bz2.sha256> | `e7e17e6c899ffe6c4cb16065d6dcaf16f9735d9cd0861099558f578d40714552` |

`CMakeLists.txt` pins the per-platform SHA-256 values. `cmake/DownloadCEF.cmake` passes the pin as CMake's `EXPECTED_HASH SHA256=...`, independently hashes any existing archive before use, and fails closed on a mismatch. A cached extracted tree is reused only when it has the matching archive-verification marker; older/unmarked trees are re-extracted from the pinned archive. The same-version archive is not trusted through an upstream SHA-1 sidecar.

These values protect against an archive that differs from the reviewed pin; the expected values were obtained from the same official CEF distribution service, not from an independent transparency log. No independent archive mirror or reproducible-build comparison was available in this audit.

## Upgrade / review procedure

- Review the official CEF stable index **quarterly**, before each release, and promptly when Chromium/CEF publishes a relevant security update. Record the review date, exact CEF and Chromium versions, platform archive URLs, and each official `.sha256` value.
- Check relevant upstream security advisories from Chromium/CEF and NVD before deciding whether a build needs an urgent update. Do not infer vulnerability status from major-version distance or invent a CVE claim. The present audit did not produce a per-CVE applicability matrix.
- Update `CEF_VERSION`, both per-platform SHA-256 pins, `CEF_VERSION_KEY` in `.github/workflows/build.yml`, Chromium version constants used by extension/update code, and this document in one reviewable change. Confirm the archive names and sidecars for both Windows x64 and macOS Intel match the exact new version/channel.
- Run the full Windows x64 and macOS Intel build, CEF sandbox checks, installer/DMG acceptance checks, and security regression suite. Verify that the CEF archive pin is exercised on clean caches as well as a warm cache. Do not call a CEF upgrade complete until both platform results pass.
- Keep CEF update commits separate from release-key rotation and platform signing changes. The workflow does not silently follow upstream `stable` and does not auto-update CEF.

## Offline stale-build warning

The About page displays a local warning when the compile-time UTC build timestamp is at least **90 days** old. It reads only the timestamp embedded by CMake and the local system clock; it makes no network request, does not query GitHub/CEF, and cannot know whether a newer release exists. Invalid or future timestamps do not produce a stale warning. A stale warning is a maintenance reminder, not a security verdict or an update check.

The warning is covered by `tests/app_build_freshness_test.js` (90-day boundary, recent/future/invalid dates, and no network/native bridge calls). The test has been added; its result is recorded in the P0 audit report. No Windows/macOS runtime UI test has been performed for this task.
