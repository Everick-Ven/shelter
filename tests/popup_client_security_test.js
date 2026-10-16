'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const clients = read('src/clients.cc');
const header = read('src/clients.h');
const shell = read('src/shell.cc');
const scheme = read('src/ui_scheme.cc');
const renderer = read('src/renderer_app.cc');

const from = (text, start, end) => {
  const i = text.indexOf(start);
  assert.notEqual(i, -1, `нет исходного раздела: ${start}`);
  const j = text.indexOf(end, i + start.length);
  assert.notEqual(j, -1, `нет границы исходного раздела: ${end}`);
  return text.slice(i, j);
};

// UI не создаёт popup, а вкладка не может открыть привилегированную схему.
const uiPopup = from(clients, 'bool UiClient::OnBeforePopup(',
                     'void UiClient::OnAfterCreated(');
assert.match(uiPopup, /return true/);
const tabPopup = from(clients, 'bool TabClient::OnBeforePopup(',
                      'bool TabClient::OnOpenURLFromTab(');
assert.match(tabPopup, /IsShelterSchemeUrl\(url\)/);
assert.ok(tabPopup.indexOf('IsShelterSchemeUrl(url)') <
          tabPopup.indexOf('client = new PopupClient()'));
assert.match(tabPopup, /CEF_WOD_NEW_POPUP && user_gesture/);
const tabOpenUrl = from(clients, 'bool TabClient::OnOpenURLFromTab(',
                        'void TabClient::OnAfterCreated(');
assert.match(tabOpenUrl, /IsShelterSchemeUrl\(target\)/);

// PopupClient явно подключает тот же запрет разрешений и загрузок.
const popupClass = from(header, 'class PopupClient : public CefClient,', '\n};');
assert.match(popupClass, /public CefDownloadHandler/);
assert.match(popupClass, /public CefPermissionHandler/);
assert.match(popupClass, /GetDownloadHandler\(\)/);
assert.match(popupClass, /GetPermissionHandler\(\)/);
assert.match(popupClass, /OnRequestMediaAccessPermission\(/);
assert.match(popupClass, /OnShowPermissionPrompt\(/);
assert.match(popupClass, /CanDownload\(/);
assert.match(popupClass, /OnBeforeDownload\(/);

const popupStart = clients.indexOf('// PopupClient\n');
assert.notEqual(popupStart, -1, 'PopupClient implementation must exist');
const popup = clients.slice(popupStart, clients.lastIndexOf('}  // namespace shelter'));
const popupBeforePopup = from(popup, 'bool PopupClient::OnBeforePopup(',
                              'void PopupClient::OnAddressChange(');
assert.match(popupBeforePopup, /return true/);
const popupBeforeBrowse = from(popup, 'bool PopupClient::OnBeforeBrowse(',
                               'bool PopupClient::OnOpenURLFromTab(');
assert.match(popupBeforeBrowse, /IsShelterSchemeUrl\(request->GetURL\(\)\.ToString\(\)\)/);
assert.match(popupBeforeBrowse, /if \(!request\) return true/);
const popupOpenUrl = from(popup, 'bool PopupClient::OnOpenURLFromTab(',
                          'CefRefPtr<CefResourceRequestHandler> PopupClient::GetResourceRequestHandler(');
assert.match(popupOpenUrl, /return true/);

const permission = from(popup, 'bool PopupClient::OnRequestMediaAccessPermission(',
                        'bool PopupClient::CanDownload(');
assert.match(permission, /NotePermissionRequest\(origin\.ToString\(\), requested_permissions,\s*true\)/);
assert.match(permission, /callback->Cancel\(\)/);
assert.match(permission, /Continue\(CEF_PERMISSION_RESULT_DENY\)/);

const download = from(popup, 'bool PopupClient::CanDownload(',
                      'bool PopupClient::OnBeforeBrowse(');
assert.match(download, /CanDownload[\s\S]*?return false/);
assert.match(download, /OnBeforeDownload[\s\S]*?return false/);
assert.match(download, /OnDownloadUpdated[\s\S]*?callback->Cancel\(\)/);

// Сетевые проверки popup идут в том же порядке, что и у обычной вкладки.
const resourceLoad = from(popup, 'cef_return_value_t PopupClient::OnBeforeResourceLoad(',
                          '\n}\n');
assert.match(resourceLoad, /IsShelterSchemeUrl/);
assert.match(resourceLoad, /StrictScriptCheck/);
assert.match(resourceLoad, /HttpsOnlyUpgrade/);
assert.match(resourceLoad, /BlockerCheck/);
assert.ok(resourceLoad.indexOf('StrictScriptCheck') <
          resourceLoad.indexOf('HttpsOnlyUpgrade'));
assert.ok(resourceLoad.indexOf('HttpsOnlyUpgrade') <
          resourceLoad.indexOf('BlockerCheck'));

// Заголовок окна начинается с проверяемого URL, очищает bidi/control chars и ограничен по длине.
const title = from(clients, 'std::string PopupWindowTitleForDisplay(',
                   '// Догрузка иконок по запросу UI.');
assert.match(title, /PopupOriginForDisplay\(url\)/);
assert.match(title, /SafePopupText\(page_title, 120\)/);
assert.match(title, /SHELTER · /);
assert.match(clients, /CefFormatUrlForSecurityDisplay\(CefString\(url\)\)/);
assert.match(clients, /IsBidiControl/);
assert.match(popup, /OnAddressChange[\s\S]*?UpdatePopupWindowTitle/);
assert.match(popup, /OnTitleChange[\s\S]*?UpdatePopupWindowTitle/);

// URL и query bridge остаются недоступны popup даже при попытке внутренней навигации.
const bridge = from(clients, 'class UiClient::BridgeHandler', 'UiClient::UiClient()');
assert.match(bridge, /!Shell::Get\(\)\.IsUiBrowser\(browser\)/);
assert.match(bridge, /!IsUiUrl\(frame->GetURL\(\)\.ToString\(\)\)/);
assert.doesNotMatch(popup, /router_/);
const uiScheme = from(scheme, 'class UiSchemeHandlerFactory',
                      '\n};\n\n}  // namespace');
assert.match(uiScheme, /!frame->IsMain\(\)/);
assert.match(uiScheme, /!Shell::Get\(\)\.IsUiBrowserId\(browser->GetIdentifier\(\)\)/);
const rendererContext = from(renderer, 'void AppBase::OnContextCreated(',
                             'void AppBase::OnContextReleased(');
assert.match(rendererContext, /if \(router_ && IsUiFrame\(frame\)\)/);

// Views callback создаёт собственное окно и показывает проверенный адрес до показа.
const popupWindow = from(shell, 'class PopupWindowDelegate', '// ---- расширения:');
assert.match(popupWindow, /SetTitle\(PopupWindowTitleForDisplay\(url, title\)\)/);
assert.ok(popupWindow.indexOf('SetTitle(PopupWindowTitleForDisplay') <
          popupWindow.indexOf('window->Show()'));
const popupView = from(shell, 'bool ShellBrowserViewDelegate::OnPopupBrowserViewCreated(',
                       '// ============================================================================\n// Shell');
assert.match(popupView, /new PopupWindowDelegate\(popup_browser_view, is_devtools\)/);
assert.match(popupView, /return true/);

console.log('Popup security source checks passed');
