#include "src/clients.h"

#include <string>

#include "include/cef_command_line.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"
#include "src/blocker.h"
#include "src/common.h"
#include "src/netguard.h"
#include "src/shell.h"

namespace shelter {

namespace {

bool IsUiUrl(const std::string& url) {
  return url.rfind(kUiOriginPrefix, 0) == 0;
}

std::string HtmlEscape(const std::string& s) {
  std::string r;
  for (char c : s) {
    switch (c) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      default: r += c;
    }
  }
  return r;
}

std::string ErrorPageDataUrl(const std::string& url, const std::string& text) {
  const std::string html =
      "<!doctype html><html lang=ru><meta charset=utf-8>"
      "<meta name=color-scheme content='dark light'>"
      "<title>Не удалось открыть страницу</title><style>"
      "html,body{height:100%;margin:0}"
      "body{display:grid;place-items:center;background:#0E0F13;color:#EDEEF2;"
      "font:15px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}"
      "main{max-width:520px;padding:32px}h1{font-size:22px;margin:0 0 8px;font-weight:650}"
      "p{color:#A2A5AF;margin:6px 0}code{color:#EDEEF2;word-break:break-all}"
      "a{display:inline-block;margin-top:18px;padding:10px 18px;border-radius:12px;"
      "background:#1D1E25;color:#EDEEF2;text-decoration:none;border:1px solid #ffffff1a}"
      "a:hover{background:#252630}</style><main>"
      "<h1>Не удалось открыть страницу</h1>"
      "<p><code>" + HtmlEscape(url) + "</code></p>"
      "<p>" + HtmlEscape(text) + "</p>"
      "<a href=\"" + HtmlEscape(url) + "\">Повторить</a></main>";
  return "data:text/html;charset=utf-8;base64," +
         CefURIEncode(CefBase64Encode(html.data(), html.size()), false)
             .ToString();
}

// Интерстишиал HTTPS-only: сайт не ответил на поднятый до https:// запрос.
// Даём осознанный выбор — вернуться или продолжить по незащищённому http://
// (как «Always Use Secure Connections» в Chrome). Хост к этому моменту уже
// внесен в исключения (netguard::TakeUpgradeFallback), поэтому переход по
// ссылке не зациклится повторным апгрейдом.
std::string HttpsOnlyInterstitialDataUrl(const std::string& http_url,
                                         const std::string& https_url) {
  const std::string html =
      "<!doctype html><html lang=ru><meta charset=utf-8>"
      "<meta name=color-scheme content='dark light'>"
      "<title>Сайт не отвечает по HTTPS</title><style>"
      "html,body{height:100%;margin:0}"
      "body{display:grid;place-items:center;background:#0E0F13;color:#EDEEF2;"
      "font:15px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}"
      "main{max-width:560px;padding:32px}"
      ".ic{width:54px;height:54px;border-radius:16px;display:grid;place-items:center;"
      "background:#2A1D12;color:#FF9A4D;margin-bottom:18px}"
      "h1{font-size:22px;margin:0 0 8px;font-weight:650}"
      "p{color:#A2A5AF;margin:6px 0}code{color:#EDEEF2;word-break:break-all}"
      ".row{display:flex;gap:10px;margin-top:20px;flex-wrap:wrap}"
      "a{display:inline-block;padding:10px 18px;border-radius:12px;text-decoration:none;"
      "border:1px solid #ffffff1a}"
      ".back{background:#1D1E25;color:#EDEEF2}.back:hover{background:#252630}"
      ".go{background:#3A2415;color:#FFB066}.go:hover{background:#4A2E1B}"
      ".note{font-size:13px;color:#7C808B;margin-top:14px}</style><main>"
      "<div class=ic><svg width=28 height=28 viewBox='0 0 24 24' fill=none "
      "stroke=currentColor stroke-width=1.7 stroke-linecap=round "
      "stroke-linejoin=round><rect x=4 y=10.5 width=16 height=10 rx=2.5/>"
      "<path d='M8 10.5V7.5a4 4 0 0 1 8 0v3'/><path d='M12 14.5v2.5'/></svg></div>"
      "<h1>Сайт не отвечает по HTTPS</h1>"
      "<p>SHELTER открыл адрес по защищённому соединению, но сайт не поддержал "
      "его:</p><p><code>" + HtmlEscape(https_url) + "</code></p>"
      "<p>Можно вернуться назад или перейти по незащищённому адресу. В режиме "
      "без шифрования содержимое страницы и введённые данные видны оператору "
      "связи и владельцу сети.</p>"
      "<div class=row>"
      "<a class=back href=javascript:history.back()>Назад</a>"
      "<a class=go href=\"" + HtmlEscape(http_url) + "\">Продолжить без "
      "шифрования</a></div>"
      "<p class=note>Для этого сайта незащищённое соединение разрешено до "
      "перезапуска браузера. Отключить HTTPS-only можно в Настройках → "
      "Приватность.</p></main>";
  return "data:text/html;charset=utf-8;base64," +
         CefURIEncode(CefBase64Encode(html.data(), html.size()), false)
             .ToString();
}

}  // namespace

// ============================================================================
// UiClient
// ============================================================================

class UiClient::BridgeHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    CEF_REQUIRE_UI_THREAD();
    // Мост принимает вызовы только от UI-браузера и только со страницы UI.
    if (!Shell::Get().IsUiBrowser(browser) || !frame ||
        !IsUiUrl(frame->GetURL().ToString())) {
      callback->Failure(403, "forbidden");
      return true;
    }
    CefRefPtr<CefValue> v = ParseJson(request.ToString());
    if (!v || v->GetType() != VTYPE_DICTIONARY) {
      callback->Failure(400, "bad request");
      return true;
    }
    CefRefPtr<CefDictionaryValue> d = v->GetDictionary();
    // Легаси-протокол инлайн-моста index.html ({ns:'shelter', cmd:'...'}):
    // host-bridge.js объявлен до первого <script>, поэтому большинство хуков
    // UI уходит через view.*, но ui:ready / tab:reload / viewport:sync инлайн
    // всё ещё шлёт напрямую. Переводим нужные команды и молча принимаем
    // остальные (иначе наблюдатели дали бы поток 404-ошибок).
    if (!d->HasKey("m")) {
      const std::string cmd = d->GetString("cmd").ToString();
      if (cmd == "ui:ready") {
        Shell::Get().OnUiReady(d->GetString("version").ToString());
      } else if (cmd == "tab:reload") {
        Shell::Get().TabAction(d->GetString("tabId").ToString(), "reload", "");
      }
      callback->Success("{\"ok\":true}");
      return true;
    }
    const std::string method = d->GetString("m").ToString();
    CefRefPtr<CefDictionaryValue> args =
        d->HasKey("a") && d->GetType("a") == VTYPE_DICTIONARY
            ? d->GetDictionary("a")
            : CefDictionaryValue::Create();
    if (!Shell::Get().HandleBridge(browser, method, args, callback)) {
      callback->Failure(404, "unknown method: " + method);
    }
    return true;
  }
};

UiClient::UiClient() {
  CefMessageRouterConfig config;
  router_ = CefMessageRouterBrowserSide::Create(config);
  bridge_ = std::make_unique<BridgeHandler>();
  router_->AddHandler(bridge_.get(), false);
}

UiClient::~UiClient() {
  if (router_ && bridge_) router_->RemoveHandler(bridge_.get());
}

bool UiClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefProcessId source_process,
                                        CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

bool UiClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                             const CefString&, const CefString&,
                             cef_window_open_disposition_t, bool,
                             const CefPopupFeatures&, CefWindowInfo&,
                             CefRefPtr<CefClient>&, CefBrowserSettings&,
                             CefRefPtr<CefDictionaryValue>&, bool*) {
  return true;  // UI не открывает окна
}

void UiClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserCreated();
  Shell::Get().OnUiCreated(browser);
}

void UiClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnBeforeClose(browser);
  Shell::Get().OnUiClosed(browser);
  Shell::Get().OnBrowserClosed();
}

bool UiClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefRequest> request,
                              bool user_gesture,
                              bool is_redirect) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnBeforeBrowse(browser, frame);
  // UI никогда не уходит со своей страницы (например, при drop ссылки на окно).
  if (frame->IsMain()) {
    const std::string url = request->GetURL().ToString();
    if (!IsUiUrl(url)) {
      return true;
    }
  }
  return false;
}

void UiClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                         TerminationStatus status, int,
                                         const CefString&) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnRenderProcessTerminated(browser);
  if (!Shell::Get().closing()) {
    browser->Reload();
  }
}

void UiClient::OnDraggableRegionsChanged(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame>,
    const std::vector<CefDraggableRegion>& regions) {
  CEF_REQUIRE_UI_THREAD();
  if (Shell::Get().IsUiBrowser(browser)) {
    Shell::Get().SetDraggableRegions(regions);
  }
}

void UiClient::OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                   CefRefPtr<CefContextMenuParams>,
                                   CefRefPtr<CefMenuModel> model) {
  model->Clear();
}

bool UiClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                             const CefKeyEvent& event, CefEventHandle,
                             bool*) {
  if (event.type == KEYEVENT_RAWKEYDOWN && event.windows_key_code == 0x7B) {
    static const bool enabled =
        CefCommandLine::GetGlobalCommandLine()->HasSwitch("ui-devtools");
    if (enabled) {
      CefWindowInfo wi;
      CefBrowserSettings bs;
      browser->GetHost()->ShowDevTools(wi, nullptr, bs, CefPoint());
      return true;
    }
  }
  return false;
}

// ============================================================================
// TabClient
// ============================================================================

bool TabClient::OnBeforeBrowse(CefRefPtr<CefBrowser>,
                                CefRefPtr<CefFrame>,
                                CefRefPtr<CefRequest> request,
                                bool,
                                bool) {
  CEF_REQUIRE_UI_THREAD();
  if (!request) return true;
  CefURLParts parts;
  if (!CefParseURL(request->GetURL(), parts)) return false;
  std::string scheme = CefString(&parts.scheme).ToString();
  for (char& c : scheme) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  // Never let a web tab load the privileged shelter:// UI origin.
  return scheme == kUiScheme;
}

// ---- блокировка трекеров/рекламы (сетевой этап) ----------------------------

namespace {

// Отменяет сторонние запросы к доменам-трекерам и рекламные URL-шаблоны.
// Навигации основного фрейма не трогаем: сайт, открытый пользователем
// осознанно, обязан загрузиться.
cef_return_value_t BlockerCheck(CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefRequest> request) {
  if (!request || !::shelter::blocker::Enabled()) return RV_CONTINUE;
  const std::string url = request->GetURL().ToString();
  if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
    return RV_CONTINUE;
  if (frame && frame->IsMain()) return RV_CONTINUE;
  char kind = 0;
  const std::string page = frame ? frame->GetURL().ToString() : std::string();
  if (!::shelter::blocker::ShouldBlock(url, page, &kind)) return RV_CONTINUE;
  std::string host;
  CefURLParts parts;
  if (CefParseURL(CefString(url), parts))
    host = CefString(&parts.host).ToString();
  ::shelter::blocker::RecordBlock(host, kind);
  return RV_CANCEL;
}

// HTTPS-only (стандарт октября 2026 — «Always Use Secure Connections»,
// Chrome 154 включает его всем по умолчанию): навигации основного фрейма
// поднимаются с http:// на https:// прямо в запросе. Если поднятый адрес не
// откроется, OnLoadError покажет интерстишиал с переходом на исходный http://.
void HttpsOnlyUpgrade(CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request) {
  if (!request || !frame || !frame->IsMain()) return;
  const std::string url = request->GetURL().ToString();
  auto upgraded = ::shelter::netguard::TryUpgrade(url);
  if (!upgraded) return;
  ::shelter::netguard::RememberUpgrade(*upgraded, url);
  request->SetURL(*upgraded);
  std::string host;
  CefURLParts parts;
  if (CefParseURL(CefString(*upgraded), parts))
    host = CefString(&parts.host).ToString();
  ::shelter::netguard::RecordUpgrade(host);
}

// Строгий режим: сторонние скрипты отменяются на сетевом уровне, пока сайт не
// получит разрешение пользователя (модель Brave Shields / NoScript).
bool StrictScriptCheck(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefRequest> request) {
  if (!request || !::shelter::netguard::StrictEnabled()) return false;
  if (request->GetResourceType() != RT_SCRIPT) return false;
  const std::string url = request->GetURL().ToString();
  std::string page;
  if (browser) {
    if (CefRefPtr<CefFrame> main = browser->GetMainFrame())
      page = main->GetURL().ToString();
  }
  if (!::shelter::netguard::ShouldBlockScript(url, page)) return false;
  CefURLParts parts;
  std::string script_host, page_host;
  if (CefParseURL(CefString(url), parts))
    script_host = CefString(&parts.host).ToString();
  if (CefParseURL(CefString(page), parts))
    page_host = CefString(&parts.host).ToString();
  ::shelter::netguard::RecordScriptBlock(page_host, script_host);
  return true;
}

}  // namespace

CefRefPtr<CefResourceRequestHandler> TabClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool,
    bool, const CefString&, bool&) {
  return this;
}

cef_return_value_t TabClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, CefRefPtr<CefCallback>) {
  if (StrictScriptCheck(browser, request)) return RV_CANCEL;
  HttpsOnlyUpgrade(frame, request);
  return BlockerCheck(frame, request);
}

bool TabClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame>, int,
                              const CefString& target_url,
                              const CefString&,
                              cef_window_open_disposition_t disposition,
                              bool user_gesture, const CefPopupFeatures&,
                              CefWindowInfo&, CefRefPtr<CefClient>& client,
                              CefBrowserSettings&,
                              CefRefPtr<CefDictionaryValue>&, bool*) {
  CEF_REQUIRE_UI_THREAD();
  // window.open(..., 'features') по жесту пользователя — настоящее окно
  // (нужно для OAuth/оплат, где важен window.opener). Всё остальное — вкладка.
  if (disposition == CEF_WOD_NEW_POPUP && user_gesture) {
    client = new PopupClient();
    return false;
  }
  Shell::Get().OnTabNewWindow(browser, target_url.ToString());
  return true;
}

bool TabClient::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame>, const CefString& url,
                                 cef_window_open_disposition_t, bool) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabNewWindow(browser, url.ToString());
  return true;
}

void TabClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserCreated();
  Shell::Get().OnTabCreated(browser, tab_id_);
}

void TabClient::OnBeforeClose(CefRefPtr<CefBrowser>) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserClosed();
}

void TabClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                     bool isLoading, bool, bool) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabLoading(browser, isLoading);
}

void TabClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                            const CefString& errorText,
                            const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain() || errorCode == ERR_ABORTED) return;
  if (Tab* tab = Shell::Get().FindTabByBrowser(browser->GetIdentifier())) {
    tab->error_url = failedUrl.ToString();
  }
  const std::string failed = failedUrl.ToString();
  // Неудавшийся апгрейд HTTPS-only: вместо общей страницы ошибки показываем
  // интерстишиал с осознанным переходом на исходный незащищённый адрес.
  if (auto http_url = ::shelter::netguard::TakeUpgradeFallback(failed)) {
    frame->LoadURL(HttpsOnlyInterstitialDataUrl(*http_url, failed));
    return;
  }
  frame->LoadURL(ErrorPageDataUrl(failed, errorText.ToString()));
}

void TabClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain()) return;
  Shell::Get().OnTabAddress(browser, url.ToString());
}

void TabClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                              const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabTitle(browser, title.ToString());
}

void TabClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            cef_transition_type_t) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabLoadStart(browser, frame);
}

void TabClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame, int) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabLoadEnd(browser, frame);
}

// Контент вкладки запросил полноэкранный режим (видео, презентация, F11 на
// странице). Сообщаем UI: он спрячет хром и растянет вкладку на всё окно,
// после чего попросит оболочку перевести окно в immersive-fullscreen.
void TabClient::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                                       bool fullscreen) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabFullscreen(browser, fullscreen);
}

bool TabClient::OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t,
                                 const CefString& message, const CefString&,
                                 int) {
  CEF_REQUIRE_UI_THREAD();
  const std::string m = message.ToString();
  // Маркер из внедрённого скрипта анти-отпечатка: считаем честно применённые
  // защиты (canvas/webgl/audio) и не засоряем консоль маркером.
  if (m.rfind("__SHELTER_FP__:", 0) == 0) {
    Shell::Get().UiEvent("blocked", "{\"f\":1}");
    return true;
  }
  return false;
}

bool TabClient::RunContextMenu(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefContextMenuParams> params,
                               CefRefPtr<CefMenuModel>,
                               CefRefPtr<CefRunContextMenuCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  callback->Cancel();  // меню рисует UI (window.shelterCtxMenu)
  Shell::Get().OnTabContextMenu(browser, frame, params);
  return true;
}

bool TabClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefDownloadItem> item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabDownloadBefore(browser, item, suggested_name.ToString(),
                                   callback);
  return true;  // решение придёт из UI (dl.decision)
}

void TabClient::OnDownloadUpdated(
    CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem> item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabDownloadUpdated(item, callback);
}

void TabClient::OnFindResult(CefRefPtr<CefBrowser> browser, int, int count,
                             const CefRect&, int activeMatchOrdinal,
                             bool finalUpdate) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnTabFindResult(browser, count, activeMatchOrdinal, finalUpdate);
}

bool TabClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                              const CefKeyEvent& event, CefEventHandle,
                              bool*) {
  CEF_REQUIRE_UI_THREAD();
  if (event.type != KEYEVENT_RAWKEYDOWN) return false;
  return Shell::Get().OnTabKey(browser, event);
}

// ============================================================================
// PopupClient
// ============================================================================

void PopupClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserCreated();
  Shell::Get().PushFpState(browser);
}

void PopupClient::OnBeforeClose(CefRefPtr<CefBrowser>) {
  CEF_REQUIRE_UI_THREAD();
  Shell::Get().OnBrowserClosed();
}

void PopupClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (auto view = CefBrowserView::GetForBrowser(browser)) {
    if (auto window = view->GetWindow()) window->SetTitle(title);
  }
}

// Полноэкранный режим контента (видео «во весь экран») в отдельном окне.
void PopupClient::OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                                         bool fullscreen) {
  CEF_REQUIRE_UI_THREAD();
  if (auto view = CefBrowserView::GetForBrowser(browser)) {
    if (auto window = view->GetWindow()) window->SetFullscreen(fullscreen);
  }
}

// Всплывающие окна — тоже веб-контент: трекеры блокируем и в них.
CefRefPtr<CefResourceRequestHandler> PopupClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool,
    bool, const CefString&, bool&) {
  return this;
}

cef_return_value_t PopupClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, CefRefPtr<CefCallback>) {
  if (StrictScriptCheck(browser, request)) return RV_CANCEL;
  return BlockerCheck(frame, request);
}

}  // namespace shelter
