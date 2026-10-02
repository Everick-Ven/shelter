# Architecture

C++ application with CEF Views. The internal SHELTER UI is a dedicated CEF browser (`shelter://ui/index.html`); each web tab is a separate CEF browser instance positioned over the UI's `#viewport` rectangle. UI requests go through the origin-validated message-router bridge (`app/cef/bridge.cc`) to BrowserController and TabManager. Windows and macOS implementations remain in separate platform directories.

Lifecycle: application startup → CEF subprocess dispatch → CEF initialization → native window → UI browser → tabs → message loop → close tabs/windows → CEF shutdown → exit.

## Browser controller

`BrowserController` owns browser-independent navigation state through `TabManager`. CEF clients report URL/title/loading events to this layer and forward them to the UI through `window.shelterCefDispatch` events (`title`, `loading`, `visit`, `window-state`) and `pushTabHist`. This keeps tab lifecycle and navigation testable without launching CEF.
