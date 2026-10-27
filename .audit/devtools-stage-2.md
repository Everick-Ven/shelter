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

Существующие файлы `.audit/` не изменялись; этот журнал создан отдельным файлом. Не выполнялись `git add`, reset, clean или удаление пользовательских изменений. Посторонние изменения рабочего дерева не включались в staging.
