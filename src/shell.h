// SHELTER — ядро оболочки: одно окно (CEF Views), chrome-UI и вкладки.
//
//  ┌─ CefWindow (frameless) ────────────────────────────────────────────┐
//  │  CefBrowserView #0  — chrome-UI (shelter://app/index.html)         │
//  │  ┌────────────────────────────────────────────────────────────┐   │
//  │  │ overlay: CefBrowserView вкладки (по одной на вкладку UI),   │   │
//  │  │ накладывается на прямоугольник #viewport из вёрстки         │   │
//  │  └────────────────────────────────────────────────────────────┘   │
//  └────────────────────────────────────────────────────────────────────┘
//
// Вкладки = отдельные CefBrowser с собственным CefRequestContext на
// «пространство» (persist:space-<key>) либо in-memory контекстом для режима
// «Призрак» (temp:ghost-<key>). Всё состояние вкладок (история, заголовки,
// пространства) остаётся в UI, оболочка только показывает страницы и шлёт события.
#ifndef SHELTER_SHELL_H_
#define SHELTER_SHELL_H_

#include <array>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_download_handler.h"
#include "include/cef_registration.h"
#include "include/cef_request_context.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_message_router.h"

namespace shelter {

class TabClient;
class UiClient;

struct Tab {
  std::string id;
  std::string partition;
  std::string requested_url;  // последний URL, который попросил UI
  std::string current_url;    // текущий адрес в браузере вкладки
  std::string find_text;      // текущий запрос поиска по странице
  bool find_kick = false;     // после нового запроса выделить первое совпадение
  std::string error_url;      // URL, не открывшийся (показана страница ошибки)
  bool loading = false;
  int browser_id = 0;
  CefRefPtr<CefBrowserView> view;
  CefRefPtr<CefOverlayController> overlay;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<TabClient> client;
  std::string pending_url;  // если браузер ещё не создан
  CefRefPtr<CefDevToolsMessageObserver> snap_observer;
  CefRefPtr<CefRegistration> snap_registration;
  // Вкладка ждёт инициализации дискового профиля (контекст ещё не готов).
  bool awaiting_context = false;
  CefRect pending_rect;
  bool has_pending_layout = false;
  bool pending_visible = false;
  bool pending_focus = false;
};

struct PendingDownload {
  CefRefPtr<CefBeforeDownloadCallback> callback;
  std::string filename;
};

struct ActiveDownload {
  CefRefPtr<CefDownloadItemCallback> callback;
};

class Shell {
 public:
  static Shell& Get();

  // ---- жизненный цикл ----
  void Start();                 // UI-поток, после OnContextInitialized
  void RequestClose();          // Cmd+Q / закрытие из UI
  void MarkDeleteUserDataOnExit() { delete_user_data_on_exit_ = true; }
  bool delete_user_data_on_exit() const { return delete_user_data_on_exit_; }
  void PrepareForShutdown();
  void OnBrowserCreated();      // счётчик живых браузеров (все клиенты)
  void OnBrowserClosed();
  bool closing() const { return closing_; }
  // Chrome создаёт дисковый профиль асинхронно; браузер вкладки можно
  // привязывать к контексту только после его инициализации. Вызывается
  // хэндлером контекста (публичный, т.к. хэндлер живёт вне класса).
  void OnContextReady(const std::string& partition);

  // ---- окно ----
  bool CanCloseWindow();
  void OnWindowCreated(CefRefPtr<CefWindow> window);
  void OnWindowDestroyed();
  void TrackPopupWindow(CefRefPtr<CefWindow> window);
  void UntrackPopupWindow(CefRefPtr<CefWindow> window);
  CefRefPtr<CefWindow> window() const { return window_; }

  // ---- UI-браузер ----
  CefRefPtr<CefBrowserView> CreateUiView();
  void OnUiCreated(CefRefPtr<CefBrowser> browser);
  void OnUiClosed(CefRefPtr<CefBrowser> browser);
  bool IsUiBrowser(CefRefPtr<CefBrowser> browser) const;
  bool IsUiBrowserId(int browser_id) const;
  void SetDraggableRegions(const std::vector<CefDraggableRegion>& regions);
  // Вызвать window.__shelterHost.ev(name, payload).
  void UiEvent(const std::string& name, const std::string& payload_json);

  // UI boot handshake: версия UI для заголовка окна (легаси-протокол ui:ready).
  void OnUiReady(const std::string& version);

  // ---- вызовы моста (UI -> native) ----
  // Возвращает true, если метод известен (и callback будет вызван).
  bool HandleBridge(CefRefPtr<CefBrowser> browser,
                    const std::string& method,
                    CefRefPtr<CefDictionaryValue> args,
                    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

  // ---- события вкладок (TabClient -> Shell) ----
  Tab* FindTab(const std::string& id);
  Tab* FindTabByBrowser(int browser_id);
  void OnTabCreated(CefRefPtr<CefBrowser> browser, const std::string& id);
  void OnTabAddress(CefRefPtr<CefBrowser> browser, const std::string& url);
  void OnTabTitle(CefRefPtr<CefBrowser> browser, const std::string& title);
  void OnTabLoading(CefRefPtr<CefBrowser> browser, bool loading);
  void OnTabNewWindow(CefRefPtr<CefBrowser> browser, const std::string& url);
  void OnTabFullscreen(CefRefPtr<CefBrowser> browser, bool on);
  void SetWindowFullscreen(bool on);
  void OnTabLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame);
  void OnTabLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame);

  // ---- расширения (Chrome Web Store / .crx) ----
  void ExtList(CefRefPtr<CefMessageRouterBrowserSide::Callback> cb);
  void ExtInstall(const std::string& src);
  void ExtRemove(const std::string& id);
  void ExtPick();
  void ExtToast(const std::string& text, bool err);
  void ExtPushList();
  void ExtInstallBytes(std::string bytes, const std::string& origin);
  bool OnTabKey(CefRefPtr<CefBrowser> browser, const CefKeyEvent& event);
  void OnTabFindResult(CefRefPtr<CefBrowser> browser, int count, int idx,
                       bool final_update);
  void OnTabContextMenu(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefContextMenuParams> params);
  void OnTabDownloadBefore(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefDownloadItem> item,
                           const std::string& suggested_name,
                           CefRefPtr<CefBeforeDownloadCallback> callback);
  void OnTabDownloadUpdated(
      CefRefPtr<CefDownloadItem> item,
      CefRefPtr<CefDownloadItemCallback> callback);

 private:
  Shell() = default;

  void MaybeQuit();

  // контексты (сессии)
  CefRefPtr<CefRequestContext> ContextFor(const std::string& partition);
  void ReleaseContextIfUnused(const std::string& partition);
  void ApplyPendingWipes();
  void QueueWipe(const std::string& partition);

  // вкладки
  Tab* CreateTab(const std::string& id,
                 const std::string& partition,
                 const std::string& url,
                 double zoom);
  void DestroyTab(const std::string& id);
  // Создаёт browser view вкладки, когда её контекст уже инициализирован.
  void FinishTabBrowser(Tab* t);
  bool ContextReady(const std::string& partition) const;
  void HideAllTabs();
  void LayoutTab(Tab* tab, const CefRect& rect, bool visible);
  CefRefPtr<CefBrowser> ActiveBrowser();
  void CaptureSnapshot(const std::string& tab_id, int quality,
                       CefRefPtr<CefMessageRouterBrowserSide::Callback> cb);
  void SetZoomAll(double factor);
  void ReapplyZoom(CefRefPtr<CefBrowser> browser);
  void ApplyClip(Tab* tab);
  void Log(const std::string& line);
  // Колбэки platform::WatchMainWindow — миниатюра/восстановление окна.
  static void WindowMiniaturized(void* ctx);
  static void WindowDeminiaturized(void* ctx);
  void ClearPrivacy(CefRefPtr<CefListValue> parts,
                    CefRefPtr<CefDictionaryValue> opts,
                    CefRefPtr<CefMessageRouterBrowserSide::Callback> cb);

 public:
  // Действие над вкладкой (reload/devtools/...); публично — вызывается
  // легаси-адаптером моста для команд вида tab:reload.
  void TabAction(const std::string& id,
                 const std::string& act,
                 const std::string& url);

 private:
  bool DownloadDecision(const std::string& id, const std::string& action,
                        bool show_dialog);
  bool DownloadControl(const std::string& id, const std::string& action);

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowserView> ui_view_;
  CefRefPtr<CefBrowser> ui_browser_;
  std::atomic<int> ui_browser_id_{-1};  // Published for IO-thread scheme checks.
  std::vector<CefDraggableRegion> drag_regions_;
  std::set<CefRefPtr<CefWindow>> popup_windows_;

  std::map<std::string, Tab> tabs_;  // id вкладки UI -> Tab
  std::string active_tab_;
  CefRect last_rect_;
  double zoom_ = 1.0;
  double clip_radii_[4] = {0, 0, 0, 0};
  std::vector<std::array<int, 4>> clip_holes_;
  std::map<std::string, CefRefPtr<CefRequestContext>> contexts_;
  std::map<std::string, bool> context_ready_;
  // partition -> id вкладок, ждущих инициализации контекста.
  std::map<std::string, std::vector<std::string>> context_waiters_;
  std::map<std::string, PendingDownload> pending_downloads_;
  // Downloads are surfaced only after the user accepts the confirmation prompt.
  // The accepted map also retains the CEF-suggested filename as a safe fallback.
  std::map<std::string, std::string> accepted_downloads_;
  std::map<std::string, ActiveDownload> active_downloads_;
  std::string last_ctx_tab_;

  int browser_count_ = 0;
  bool closing_ = false;
  bool quit_posted_ = false;
  bool delete_user_data_on_exit_ = false;
};

// Сравнение адресов без учёта схемы, www, завершающего '/' и регистра.
bool SameUrl(const std::string& a, const std::string& b);

// Общий делегат BrowserView: Alloy-стиль + всплывающие окна (popup/DevTools).
class ShellBrowserViewDelegate : public CefBrowserViewDelegate {
 public:
  ShellBrowserViewDelegate() = default;
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

 private:
  IMPLEMENT_REFCOUNTING(ShellBrowserViewDelegate);
  DISALLOW_COPY_AND_ASSIGN(ShellBrowserViewDelegate);
};

}  // namespace shelter

#endif  // SHELTER_SHELL_H_
