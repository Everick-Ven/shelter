# CEF

Pinned initial distribution: `154.0.32+g682c378+chromium-154.0.8037.58`, from `https://cef-builds.spotifycdn.com/`. It is downloaded externally by `scripts/bootstrap-cef.sh` and never committed.

## Phase 2

The application dispatches CEF subprocesses, initializes CEF, registers the local `shelter://ui` scheme, and opens the first browser view at `shelter://ui/index.html`. UI files are served from the app's packaged resources (`Resources/ui` on Windows and `Contents/Resources/ui` inside the macOS bundle); development builds stage the same files beside the executable. This avoids relying on the launcher's working directory and keeps the initial window on the bundled SHELTER interface instead of an external page.

On macOS, the browser executable and CEF helper are separate targets. The main app uses `CefScopedLibraryLoader::LoadInMain`; the nested `SHELTER Helper.app` uses `LoadInHelper` and is placed under `Contents/Frameworks`. SHELTER currently runs with `no_sandbox=true`, so the macOS build explicitly disables CEF's helper sandbox. CI ad-hoc signs the packaged app and nested helper, verifies the signature, then requires the packaged browser to load the bundled SHELTER UI document successfully. Runtime logs are written to `~/Library/Application Support/SHELTER/` (`shelter.log`, `shelter-cef.log`, and `shelter-helper.log`). Public distribution still requires Developer ID signing and notarization to avoid Gatekeeper warnings.
