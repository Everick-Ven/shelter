# CEF

Pinned initial distribution: `154.0.32+g682c378+chromium-154.0.8037.58`, from `https://cef-builds.spotifycdn.com/`. It is downloaded externally by `scripts/bootstrap-cef.sh` and never committed.

## Phase 2

The application dispatches CEF subprocesses, initializes CEF, creates a CEF Views top-level window, creates a first browser view and runs the CEF message loop.

## Phase 3 (UI integration)

The shell loads the SHELTER UI (`shelter://ui/index.html`) as a dedicated `CefBrowserView` that fills a native-framed top-level window. Every web tab is an additional `CefBrowserView` parented to the same window (Alloy runtime style — a Chrome-style window may host at most one Chrome-style view). Content views are positioned over the UI's `#viewport` rectangle reported by the `viewport:sync` bridge command and hidden with `SetVisible(false)` whenever the UI shows internal pages or overlays (`tab:hideContent` / `visible:false`).

Bridge:

- Renderer side: `CefMessageRouterRendererSide` is created lazily in `App::OnContextCreated` and forwarded from `OnContextReleased` / `OnProcessMessageReceived`, which provides `window.cefQuery` to every page. Only the UI browser's router has a handler; queries from content pages are auto-rejected.
- Browser side: `Client` (UI role) registers `BridgeHandler` (`app/cef/bridge.cc`). `OnQuery` validates the frame origin (`shelter://ui*`), parses the JSON request and dispatches: `window:minimize|maximize|close` (toggle-maximize via `CefWindow`), `tab:navigate` (validated `IsValidNavigationUrl` + `LoadURL`, dedupes identical URLs), `tab:hideContent`, `tab:stop`, `tab:reload`, `tab:zoom` (factor → `SetZoomLevel`, ln/ln 1.2), `tab:captureThumbs` (`{ok:false}`), `viewport:sync` (bounds/visibility/focus), `devtools:ctl` (`ShowDevTools`/`CloseDevTools`), `theme:scheme`, `privacy:clear` (global cookie delete), `clipboard:read|write` (platform natives in `app/platform`), and graceful `{ok:false}` fallbacks for `auth:open`, `ctx:act`, `window:openPlain`, `ai:test`, `ai:chat`.
- Native → UI: `ExecuteJavaScript` into `window.shelterCefDispatch(...)` for `title`, `loading`, `visit`, `window-state`, plus `pushTabHist(url, tabId)` on every main-frame address change (stack neighbor logic in the UI handles redirects and mouse back/forward) and `onNativeLoading(loading)` for the active tab. `visit` is only emitted for `TT_LINK` loads without redirect flags so omnibox navigations (recorded by the UI in `loadUrl`) are not duplicated.

UI scheme (`app/cef/ui_scheme.cc`): resolves the UI directory from the executable location (`<exe>/Resources/ui`, `<exe>/../Resources/ui` for the macOS bundle, `SHELTER_UI_DIR`, repository-relative fallbacks), serves MIME-typed files, returns no handler for missing/unsafe paths (path traversal guard), and injects a small stylesheet that hides `#winCtl`/`#winLights` because the native title bar provides window controls.

Runtime configuration (`app/main/main_common.cc`): `root_cache_path` and `cache_path` point at a per-user absolute directory (`%LOCALAPPDATA%\SHELTER` / `~/Library/Application Support/SHELTER`) created before `CefInitialize` — required by the CEF ≥120 process singleton and the only way the UI `localStorage` persists. CEF and app logs live there as well. On Windows the binary links as a GUI subsystem app (`/SUBSYSTEM:WINDOWS`, `main` entry) so no console window appears.

Lifecycle: close is orchestrated in `App::RequestWindowClose` — content browsers are force-closed first, then the UI browser (`TryCloseBrowser`), and `CefQuitMessageLoop` runs once the last browser reports `OnBeforeClose`. DevTools open as their own top-level window positioned at the UI-provided `#dtBody` bounds; downloads save to the user's Downloads folder (Alloy style cancels downloads without a handler).
