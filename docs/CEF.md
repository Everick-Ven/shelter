# CEF

Pinned initial distribution: `154.0.32+g682c378+chromium-154.0.8037.58`, from `https://cef-builds.spotifycdn.com/`. It is downloaded externally by `scripts/bootstrap-cef.sh` and never committed. The next phase adds subprocess dispatch, CEF Views clients and packaging resources.

## Phase 2

The application now dispatches CEF subprocesses, initializes CEF, creates a CEF Views top-level window, creates a first browser view and loads `https://example.com`, then runs and shuts down the CEF message loop.
