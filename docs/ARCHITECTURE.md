# Architecture

C++ application with CEF Views. The internal SHELTER UI will be a dedicated CEF browser; each web tab will be a separate CEF browser instance. UI requests will go through a validated bridge to BrowserController and TabManager. Windows and macOS implementations remain in separate platform directories.

Lifecycle: application startup → CEF subprocess dispatch → CEF initialization → native window → UI browser → tabs → message loop → close tabs/windows → CEF shutdown → exit.

## Browser controller

`BrowserController` owns browser-independent navigation state through `TabManager`. CEF clients report URL/title/loading events to this layer; the UI will consume state events through the bridge in the next phase. This keeps tab lifecycle and navigation testable without launching CEF.
