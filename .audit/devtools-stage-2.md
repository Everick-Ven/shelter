# Журнал этапа 2 — встроенный DevTools dock

**Дата:** 2026-10-27
**Статус:** статические проверки этапа 2 проходят; нативная сборка и runtime-приёмка не выполнены.

## Цель и решение

Реализовать настоящий DevTools frontend через CEF Views — не HTML-имитацию и не платформенную ручную встройку. Сохранить обычное поведение popup-окон сайта, отдать popup с `is_devtools=true` главному окну и оставить действие открепления в отдельное Views-окно.

## Изменения этапа

- `ShellBrowserViewDelegate::OnPopupBrowserViewCreated` передаёт DevTools popup в `Shell::OnDevToolsPopupBrowserViewCreated`; обычные popup'ы по-прежнему используют отдельный `CefWindow`.
- В docked-режиме `CefBrowserView` DevTools добавляется через `CefWindow::AddOverlayView(..., CEF_DOCKING_MODE_CUSTOM, ...)`. Его bounds/visibility обновляются по прямоугольнику `#dtBody`, который UI посылает отдельным маршрутом `devtools/layout`.
- UI предоставляет dock снизу/справа, resize-handle, раздельно сохраняет высоту/ширину и сторону в `S.ui`.
- Жизненный цикл DevTools связан с активным web-tab: при переключении закрывается старый target и открывается текущий; для `shelter://` реальный frontend закрывается, а в UI остаётся заглушка. `renderView` обновляет заголовок и заглушку при смене вкладки.
- Действие «Открепить в окно» убирает overlay и помещает тот же BrowserView в отдельное `CefWindow` через `PopupWindowDelegate`.
- В DevTools-коде не добавлялись HWND/NSView-вызовы. Устаревший комментарий host bridge о нереализованном dock удалён.
- В CI добавлен отдельный stage-2 source regression test. Добавлена проверка, что смена вкладки обновляет состояние stub/header.

## TDD и исправление тестов

`tests/devtools_dock_test.js` был добавлен до native popup handler; исходная проверка падала на отсутствии обработчика DevTools popup. При последующей проверке несколько падений оказались несовпадением source-regex с фактическими пробелами/переносами и полными аргументами bridge-вызова. Аналогично были уточнены два теста: геометрия контекстного клика проверяется в host bridge, а Inspect-проверка — по передаче координаты через `std::to_string`. Эти исправления меняли ожидания тестов, а не маскировали runtime-ошибку.

Итоговые команды:

| Проверка | Результат |
|---|---|
| `node tests/devtools_dock_test.js` | PASS — `DevTools dock stage-2 regression test passed` |
| `node tests/ui_devtools_test.js` | PASS — `UI_DEVTOOLS_TEST_PASS` |
| `node tests/devtools_bridge_reentrancy_test.js` | PASS — `DevTools bridge reentrancy regression test passed` |
| `node tests/ui_window_controls_test.js` | PASS — Windows controls styling/platform gating/placement/native-state checks |
| `node --check resources/ui/host-bridge.js` + проверка 5 inline JS-блоков | PASS — синтаксис корректен |
| `git diff --check` | PASS — ошибок форматирования нет |

## Проверка CEF Views API

Сверены API-поверхности под закреплённый CEF 154.0.32 (`682c378`, тот же ref, который указан в CMake pin):

- [`CefBrowserViewDelegate`](https://github.com/chromiumembedded/cef/blob/682c378/include/views/cef_browser_view_delegate.h) указывает, что `OnPopupBrowserViewCreated` вызывается после `OnAfterCreated` и `OnBrowserCreated` для popup browser; callback позволяет самостоятельно добавить popup BrowserView в Views hierarchy.
- [`CefOverlayController`](https://github.com/chromiumembedded/cef/blob/682c378/include/views/cef_overlay_controller.h), [`CefWindow`](https://github.com/chromiumembedded/cef/blob/682c378/include/views/cef_window.h) и [`CefPanel`](https://github.com/chromiumembedded/cef/blob/682c378/include/views/cef_panel.h) предоставляют Views-механизмы bounds/visibility, overlay и удаления дочернего view для переноса.
- [`CefDevToolsWindowRunner`](https://github.com/chromiumembedded/cef/blob/682c378/libcef/browser/devtools/devtools_window_runner.cc) показывает, что `ShowDevTools` выполняет открытие DevTools либо `InspectElement` по переданной точке. Этот код подтверждает путь API, но **не** доказывает отсутствие исходного crash.

## Ограничения и незакрытые расследования

- В checkout нет CEF SDK headers, `CEF_ROOT` и подготовленного build-каталога. Нативную сборку и запуск на macOS 15.8.1 / Intel x86_64 выполнить здесь нельзя. Поэтому C++-компиляция, фактическая геометрия Views overlay, переключение/открепление и визуальное поведение должны быть проверены в целевой сборке.
- Исходный crash при `CefBrowserHostCToCpp::ShowDevTools` остаётся **не подтверждённо исправленным**: отложенный вызов уже падал в приложении, а этап 2 по-прежнему использует `ShowDevTools` для создания frontend. Нельзя заявлять о runtime-устранении причины без macOS crash reproduction/stack после сборки.
- Ошибка расшифровки `shelter.board` здесь не диагностировалась: ключи и данные не менялись; для продолжения нужны `keychain.log` и stderr запуска. Пользовательские задачи не очищались.

## Сохранность рабочей копии

Существующие файлы `.audit/` не изменялись; этот журнал создан отдельным файлом. Посторонние изменения рабочего дерева не включались в staging.

## CI follow-up после push этапа 2

**Коммит:** `7148a70 feat: add CEF Views DevTools dock` (push прошёл; удалённая ветка указывала на этот SHA).

GitHub Actions Build завершился с ошибкой. DevTools regression step и native build на Windows/macOS прошли. Проверка `CSP hashes and renderer secret isolation` обнаружила, что два SHA-256 в CSP не соответствовали обновлённым inline-скриптам `resources/ui/index.html`. Геометрические acceptance checks на обеих платформах затем не дождались инициализации UI bridge (`ready=false`, `hasNavigate=false`, `hasNativeRequest=false`); это согласуется с блокировкой скриптов устаревшими CSP hashes и не указывает на отдельный сбой DevTools geometry.

**Исправление:** CSP allowlist обновлён для двух изменённых inline-скриптов. Локально повторены и прошли `node tests/ui_csp_test.js`, все четыре профильных DevTools/Windows-control теста, `node --check resources/ui/host-bridge.js`, синтаксическая проверка всех пяти inline JS-блоков и `git diff --check`. На момент записи повторный GitHub Actions runtime/geometry acceptance ещё ожидался; позднее результат зафиксирован в следующем дополнении.

## Диагностика crash report и новая проверка runtime-пути

**Пользовательский crash report:** Shelter 1.0.304 / CEF 154.0.32, macOS 15.8.1 Intel x86_64; `EXC_BAD_ACCESS (SIGSEGV)` по `0` при вызове `CefBrowserHostCToCpp::ShowDevTools(...)` из `ShowDevToolsTask::Execute()`. Сам отчёт не содержит символизированного кадра внутри libcef, достаточного для доказательства конкретного null dereference; поэтому точную причину нельзя считать подтверждённой.

**Найденный контрактный разрыв в приложении:** `ShowDevToolsTask` передавал `nullptr` как `CefClient`. В `BrowserApp` нет `CefBrowserProcessHandler::GetDefaultClient()` override, поэтому CEF fallback также возвращал `nullptr`. Документация CEF 154 указывает, что окно `CefBrowser` без client является unmanaged (без client callbacks; закрытия такого окна нужно дождаться вручную). Это конкретный дефект жизненного цикла DevTools-popup и правдоподобная причина crash внутри создания popup, хотя он не доказывает сам fault из crash report. `CefWindowInfo` пустой для Views-hosted браузера намеренно: CEF документирует, что для `CefBrowserView` этот аргумент игнорируется; вызов идёт на UI thread и сохраняет сильные `CefRefPtr` на исходный browser и host.

**Минимальная правка:** передавать отдельный ref-counted `DevToolsBrowserClient`, а не `nullptr` и не `TabClient` (последний ошибочно обрабатывал бы DevTools frontend как обычную вкладку). Добавлен `tests/devtools_client_test.js`; тест сначала воспроизвёл отсутствие client (FAIL), после исправления прошёл. Обновлена проверка reentrancy для различения отменённого task и реально завершившегося CEF вызова.

**Поведенческая regression-проверка:** Windows/macOS CI geometry smoke теперь открывает настоящую docked CEF Views DevTools popup, ждёт, что `ShowDevTools` возвратился и BrowserView прикрепился к overlay, затем повторно вызывает `ShowDevTools` с Inspect-точкой `(12, 12)` и ждёт возврата. Маркеры диагностики существуют только при `SHELTER_CI_HOOKS`; production build не пишет эти строки. Это проверит именно CEF runtime, которого не покрывали прежние source/UI tests.

**Локально пройдены:** `node tests/devtools_client_test.js`, `node tests/devtools_bridge_reentrancy_test.js`, `node tests/devtools_dock_test.js`, `node tests/ui_devtools_test.js`, `node tests/ci_hooks_test.js`, Python syntax check для `tools/check_geometry.py`, `git diff --check`. Этот sandbox не имеет CEF SDK/build и не может запустить native macOS app; новый CI runtime smoke пока не запускался на изменённом коде. Не заявлять о подтверждённом устранении исходного crash до прохождения smoke на macOS и повторной пользовательской проверки 15.8.1.

**Предыдущий CI:** run `40565861723` по commit `49dc576` был успешен для macOS Intel, Windows x64 и UI/security regression tests; в нём DevTools runtime не запускался, поэтому он не является проверкой crash fix.

## CI runtime result: reproduction on macOS and runtime-style follow-up

**Run `40571932166`, commit `f10d3d7`:** UI/security regression suite passed; native builds passed on Windows and macOS; Windows Geometry/DevTools acceptance and CI-hook runtime acceptance passed. macOS Intel's Geometry and DevTools acceptance failed on the first dock request: `SHELTER exited early with status -11 while waiting for CEF ShowDevTools to return`. The request was accepted and the UI bridge was ready, but the CEF call-return marker was never written. This is a real macOS CI reproduction of the unresolved crash after adding the explicit client; the client change alone is not a fix.

**Additional CEF diagnostic:** macOS build/runtime diagnostics include `browser_view_impl.cc:46`: `GetBrowserRuntimeStyle() requested Alloy style; only Chrome style is supported for DevTools popups`. Pinned CEF source (`682c378`) confirms `ComputeAlloyStyle(..., is_devtools_popup=true, ...)` always creates the popup with Chrome style but logs when the popup's `CefBrowserViewDelegate` requests Alloy. Its `cef_types_runtime.h` also documents that an Alloy-style Views window can host only Alloy BrowserViews, while a Chrome-style window can host both styles (at most one Chrome-style BrowserView). Thus the existing Alloy main window is incompatible with the Chrome-style DevTools view required for the dock. This is a strong, actionable hypothesis, not proof of the SIGSEGV cause; no symbolized CEF crash frame was recovered.

**TDD follow-up in the worktree:** `tests/devtools_dock_test.js` was strengthened to require Chrome style for the DevTools BrowserView delegate and its host windows; it failed before implementation. `ShellBrowserViewDelegate::GetDelegateForPopupBrowserView` now returns a dedicated Chrome-style delegate only for DevTools; the main dock host window is Chrome style while its UI/tab BrowserViews remain Alloy; the detached DevTools window is Chrome style, while ordinary page popup windows remain Alloy. This matches the pinned CEF Views style constraints. `node tests/devtools_dock_test.js` and `node tests/devtools_client_test.js` pass after the change. It is not yet built or runtime-tested; rerun macOS CI before deciding whether the style incompatibility caused or merely accompanied the crash.

**Log retrieval limitation:** the GitHub check annotation exposed the failure and CEF style message, but the full Actions log/artifact download returned EOF from the runner artifact host in this sandbox. No complete, symbolized crash report is currently available.
