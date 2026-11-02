# CEF pin and maintenance review

## Historical audit snapshot (2026-10-25)

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

## Reviewed state (2026-11-02)

| Item | Value |
|---|---|
| Project CEF pin in the working tree | `156.0.15+gcf671c0+chromium-156.0.8078.80`, minimal distribution |
| Current stable shown by the official CEF build index | `156.0.15+gcf671c0+chromium-156.0.8078.80` |
| Chromium version drift | None against the current Chrome 156 stable build reviewed on 2026-11-02. This is not a per-CVE applicability result. |
| Upgrade status | Pin and hashes updated locally; Windows x64 and macOS Intel build/runtime acceptance are **pending**. Do not call the upgrade accepted until both pass. |
| Previous pin | `154.0.32+g682c378+chromium-154.0.8037.58` (reviewed 2026-10-25); it was two Chromium majors behind. |
| Last source check | 2026-11-02; official CEF build index and archive sidecars below. No Windows/macOS CEF upgrade build was performed for this review. |

The official CEF index shows `156.0.15+gcf671c0+chromium-156.0.8078.80` under **Current Stable Build (Preferred)**: <https://cef-builds.spotifycdn.com/index.html>. The Windows x64 and macOS Intel minimal archive sidecars below were read directly on 2026-11-02. The index primarily lists SHA-1 links; these are the distribution's `.sha256` sidecars, not independently signed transparency-log attestations.

| Platform archive (minimal) | Official SHA-256 sidecar | Pinned SHA-256 |
|---|---|---|
| `cef_binary_156.0.15+gcf671c0+chromium-156.0.8078.80_windows64_minimal.tar.bz2` | <https://cef-builds.spotifycdn.com/cef_binary_156.0.15%2Bgcf671c0%2Bchromium-156.0.8078.80_windows64_minimal.tar.bz2.sha256> | `371efdf09dce30569d9a0880727be79fea96ab5ba6f7be533c9332bb0790385a` |
| `cef_binary_156.0.15+gcf671c0+chromium-156.0.8078.80_macosx64_minimal.tar.bz2` | <https://cef-builds.spotifycdn.com/cef_binary_156.0.15%2Bgcf671c0%2Bchromium-156.0.8078.80_macosx64_minimal.tar.bz2.sha256> | `e2aedb3cb41cc293ff2db81245f3462c61c49e13ac137f628872889c8a36c702` |

`CMakeLists.txt` pins the per-platform SHA-256 values. `cmake/DownloadCEF.cmake` passes the pin as CMake's `EXPECTED_HASH SHA256=...`, independently hashes any existing archive before use, and fails closed on a mismatch. A cached extracted tree is reused only when it has the matching archive-verification marker; older/unmarked trees are re-extracted from the pinned archive. The same-version archive is not trusted through an upstream SHA-1 sidecar.

These values protect against an archive that differs from the reviewed pin; the expected values were obtained from the same official CEF distribution service, not from an independent transparency log. No independent archive mirror or reproducible-build comparison was available in this audit.

## Upstream security advisory check (2026-11-02)

- Google's desktop Stable channel advanced to `156.0.8078.73/.74` for Windows/macOS on 2026-10-27, then to `156.0.8078.79/.80` on 2026-10-29: <https://chromereleases.googleblog.com/2026/10/stable-channel-update-for-desktop_0562499947.html>; <https://chromereleases.googleblog.com/2026/10/stable-channel-update-for-desktop_02107719452.html>.
- NVD describes `CVE-2026-76024` as Critical Chromium memory corruption in MediaStream that could allow reading memory inside the sandbox via a crafted HTML page; affected Chrome versions are prior to `156.0.8078.79`: <https://nvd.nist.gov/vuln/detail/CVE-2026-76024>. The project's former Chromium `154.0.8037.58` pin is below that fixed version, so its corresponding CEF build needs review against the upstream change. NVD lists Google Chrome, not CEF, as the affected product; this is a potential CEF exposure, not proof of exact CEF applicability. CEF downstream patches were not audited per CVE.
- The candidate CEF `156.0.15` embeds Chromium `156.0.8078.80`, at/after that upstream fix. This does not establish that every advisory is resolved, nor does it establish an exploitation status. A full CEF patch/CVE applicability matrix and both-platform runtime acceptance remain outstanding.

## M156 Windows startup issue review (2026-11-02)

CEF issue [#4326](https://github.com/chromiumembedded/cef/issues/4326) is open and was reproduced with CEF `156.0.14` / Chromium `156.0.8078.48`. It reports that Windows auto de-elevation now runs before `OnBeforeCommandLineProcessing`; a switch added in that callback is too late. A separate early-exit access violation is reported when `multi_threaded_message_loop = true` because the UI task runner was not created. The reporter's single-threaded `cefclient` case exits cleanly, while the multi-threaded case crashes.

SHELTER's current Windows startup in `src/main_win.cc` calls `CefExecuteProcess`, then `CefInitialize`, returns `CefGetExitCode()` on initialization failure, and otherwise uses `CefRunMessageLoop()`. No project code sets `multi_threaded_message_loop` (the CEF default is false), so the reported multi-threaded shutdown crash condition is not enabled by the current source. `BrowserApp::OnBeforeCommandLineProcessing` does not add `--do-not-de-elevate`; even if it did, issue #4326 says that callback is too late on M156. This source review narrows applicability but is **not** runtime evidence, and the issue's reproduction used CEF 156.0.14 rather than this 156.0.15 candidate.

Before Windows acceptance, test both ordinary and elevated launches on the actual 156.0.15 build. For the elevated case, use a UAC split-token administrator and verify the de-elevated relaunch, original-process exit (no access violation), normal browser startup, and sandbox/bootstrap behavior. Do not add the issue's fragile `GetCommandLineW` IAT-hook workaround without a demonstrated need; no such workaround is in this change. Record the result and exact build/run ID. The Windows runtime check remains outstanding.

## Upgrade / review procedure

- Review the official CEF stable index **quarterly**, before each release, and promptly when Chromium/CEF publishes a relevant security update. Record the review date, exact CEF and Chromium versions, platform archive URLs, and each official `.sha256` value.
- Check relevant upstream security advisories from Chromium/CEF and NVD before deciding whether a build needs an urgent update. Do not infer vulnerability status from major-version distance or invent a CVE claim. The present audit did not produce a per-CVE applicability matrix.
- Update `CEF_VERSION`, both per-platform SHA-256 pins, `CEF_VERSION_KEY` in `.github/workflows/build.yml`, Chromium version constants used by extension/update code, and this document in one reviewable change. Confirm the archive names and sidecars for both Windows x64 and macOS Intel match the exact new version/channel.
- Run the full Windows x64 and macOS Intel build, CEF sandbox checks, installer/DMG acceptance checks, and security regression suite. Verify that the CEF archive pin is exercised on clean caches as well as a warm cache. Do not call a CEF upgrade complete until both platform results pass.
- Keep CEF update commits separate from release-key rotation and platform signing changes. The workflow does not silently follow upstream `stable` and does not auto-update CEF.

## Offline stale-build warning

The About page displays a local warning when the compile-time UTC build timestamp is at least **90 days** old. It reads only the timestamp embedded by CMake and the local system clock; it makes no network request, does not query GitHub/CEF, and cannot know whether a newer release exists. Invalid or future timestamps do not produce a stale warning. A stale warning is a maintenance reminder, not a security verdict or an update check.

The warning is covered by `tests/app_build_freshness_test.js` (90-day boundary, recent/future/invalid dates, and no network/native bridge calls). The test has been added; its result is recorded in the P0 audit report. No Windows/macOS runtime UI test has been performed for this task.
