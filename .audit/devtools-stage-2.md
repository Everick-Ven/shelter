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

## CI result of runtime-style change and next isolation step

**Run `40574176677`, commit `eff61b8`:** UI/security tests and native release/CI-hook builds passed, but Geometry/DevTools acceptance failed on both platforms. macOS exited with `-11` before the `devtools dock overlay attached` marker. Windows exited with `3221225477` (`0xC0000005`) after the overlay-attached marker but before the `ShowDevTools returned` marker. The macOS diagnostics no longer contain CEF's Alloy-style warning after the DevTools-specific delegate change, yet the crash remains; the warning/style mismatch was real but not sufficient to explain the failure. Changing the main window to Chrome style also did not make acceptance pass.

**Localization and follow-up in the worktree:** on Windows, the only explicit app operation after the overlay-attached marker and before returning from CEF's synchronous popup callback was `popup_browser_view->RequestFocus()`. The next change removes that call from `OnPopupBrowserViewCreated`, requests focus only after `ShowDevTools` returns, and adds CI-only markers at callback entry, after `AddOverlayView`, after bounds, and after deferred focus. The dock test was made red first by requiring focus outside the popup callback. Local regression tests and native runtime acceptance for this latest change have not yet run. On macOS the existing trace lacks an overlay marker, so the new callback/overlay markers should distinguish a pre-callback CEF crash from an attach-stage crash. Root cause remains unproven.

## CI result after popup tracing and deferred focus

**Run `40576852952`, commit `94d886e`:** UI/security tests and both native builds passed; runtime acceptance still failed on both platforms. macOS exited `-11` with none of the new popup-callback/overlay markers, so the fault occurs before SHELTER's `OnDevToolsPopupBrowserViewCreated` and before `AddOverlayView`. Windows logged callback entry, `AddOverlayView returned`, bounds set, and overlay attached, then exited `3221225477` (`0xC0000005`) before `ShowDevTools returned`; no deferred-focus marker appeared. Deferring `RequestFocus` did not fix the Windows crash. The explicit popup delegate also removed the Alloy-style warning on macOS, but the crash persisted. The full, symbolized crash stack remains unavailable.

**Style experiment decision:** changing the main Shell window to Chrome style (commit `eff61b8`) made Windows acceptance fail after overlay attachment and did not fix macOS. The worktree now restores the existing Alloy main-window style; the per-DevTools BrowserView delegate and detached DevTools window remain Chrome style. Although pinned CEF documentation describes Alloy-window/Chrome-BrowserView hosting as unsupported, this window-style change is not a demonstrated fix and the failure trace indicates that macOS crashes before SHELTER's dock callback. Do not state that the style contract was the root cause.

**Run `40579682107`, commit `8d55521`:** UI/security tests and both native builds passed; runtime acceptance still failed on both platforms. macOS again exited `-11` with no popup-callback marker, so calling CEF from `TID_FILE_USER_VISIBLE` did not move the crash past CEF's pre-callback creation path. Windows reached callback entry, `AddOverlayView`, bounds, and overlay-attached markers, then exited `3221225477` (`0xC0000005`) before the returned marker. The off-UI dispatch therefore did not solve macOS and was not acceptable for Windows runtime; it has been reverted. The current worktree restores direct UI-thread `ShowDevTools` invocation and keeps focus deferred until the API returns, with the main window back in its original Alloy style. Source tests pass locally; native build/runtime acceptance for this latest direct-call-plus-deferred-focus combination still needs a new CI run.

## Forensic follow-up after run 40582338060

**Run `40582338060`, commit `aea8743`:** UI/security tests and native builds passed. Runtime acceptance failed on both platforms on the *first plain dock request* (not the later Inspect-point request): macOS exited `-11` before SHELTER's popup callback marker; Windows exited `3221225477` (`0xC0000005`) after callback entry, overlay creation/bounds/visibility, and attachment, but before `ShowDevTools` returned. This confirms that deferred-vs-direct dispatch and deferred focus do not explain the common failure. It still does not identify the faulting frame.

**CEF issue #4168 checked and ruled out for this CI failure:** that upstream issue is specifically the non-empty Inspect-point branch on a non-global request context. The pinned CEF source `682c378` already includes the upstream `UniqueForCEF()` profile-preservation patch, and `tools/check_geometry.py`'s failing first call requests DevTools with no Inspect point; the second Inspect call is never reached. Do not present #4168 as the cause of the plain-open crash.

**Run `40600144168`, commit `1be43f1`:** crash-report collection wiring and all UI/security tests passed; the macOS geometry acceptance still exited `-11` on the first plain `ShowDevTools` request. The new collector/upload step succeeded. Windows geometry/runtime acceptance passed in this run without a product-code change, although earlier runs failed there, so Windows is currently intermittent rather than fixed. The macOS diagnostics artifact is present (17,255 bytes), but both `gh run download` and Actions-log retrieval fail with EOF from GitHub's external result/blob hosts in this sandbox; no stack was obtained from the artifact.

**Run `40602691309`, commit `6070b34`:** UI/security tests and Windows geometry/runtime acceptance passed; macOS still failed on the first plain dock request, now with process status `-5` (SIGTRAP) rather than earlier `-11` (SIGSEGV). The request was accepted, but `ShowDevTools` did not return (`loading=true`). Collector/upload steps succeeded, but no native-crash check annotation appeared, so no matching `.ips`/`.crash` reached the reporter; why CrashReporter did not yield one remains unknown. The variation in signal is another reason not to infer a specific CEF frame from exit status alone.

**Forensic transport workaround:** `tools/crash_report_summary.py` and `tools/report_ci_failure.py` now surface `.ips`/`.crash` frames as GitHub check annotations and job-summary text; Windows `.dmp`/`.mdmp` reports produce an inventory annotation. Tests passed locally and in the CI UI/security job, but no macOS report was available to exercise the parser against a real CI report.

**Run `40604665208`, commit `93f9083`:** the macOS geometry/DevTools acceptance passed under LLDB, including dock open and Inspect; no crash stack was produced. The following runtime-acceptance step failed all three launches with status `24` while waiting for the app UI target. Windows and UI/security jobs passed. This does not prove a product fix; the debugger changes launch timing, and the harness may also have left its inferior alive when LLDB was terminated.

**Run `40607544099`, commit `6038204`:** macOS geometry/DevTools again passed under LLDB, but the log explicitly reported `LLDB cleanup: inferior PID was not present in debugger output`; all three subsequent runtime-smoke attempts again exited with status `24` while waiting for the app UI target. This confirms the cleanup lookup missed the inferior, but does not by itself prove that a lingering process caused the later failure. Windows failed again with `0xC0000005` after the popup callback, `AddOverlayView`, bounds, and overlay attachment, before `ShowDevTools` returned. No Windows dump annotation appeared. UI/security tests passed.

**Run `40609891273`, commit `c8fc46f`:** Windows geometry, runtime acceptance, packaging, and all UI/security tests passed after changing WER to machine-level LocalDumps. macOS failed again during the dock request: LLDB recorded a native thread backtrace, but the shared log annotation truncated the first frames, leaving only later CoreFoundation/CEF frames visible. This is the first direct native stack evidence, but the faulting frame and signal are not yet visible; do not infer a root cause from the partial tail.

**Run `40611589364`, commit `177c187`:** full CI passed on macOS, Windows, and UI/security; macOS geometry/DevTools including Inspect and the subsequent runtime acceptance passed while the geometry app was launched under LLDB. This is not a product fix or a diagnosis—the debugger changes launch timing, and this run did not produce a crash stack.

**Run `40613700844`, commit `dceec05`:** the direct (non-debugger) geometry check reproduced the crash on both platforms during the dock request: macOS exited `-11` (SIGSEGV), Windows `3221225477` (`0xC0000005`, access violation). On both, the CEF request had been accepted and the failure occurred while waiting for `CefBrowserHost::ShowDevTools` to return. This cross-platform repeat establishes a native crash at that operation, but not the faulting frame or root cause. The LLDB retry was triggered but marked failed before its diagnostic output was annotated.

**LLDB/WER capture correction:** LLDB cleanup falls back to `ps` using exact executable + remote-debugging port; Windows LocalDumps uses `HKLM\SOFTWARE\...\LocalDumps\Shelter.exe`. A compact faulting-thread parser appends the stop-reason thread and leading frames from `#0` to the diagnostic log. Run `40613700844` exposed that the retry shell inherited GitHub's `bash -e`; its failing pipeline aborted before the script could report/move its logs. Commit `c9d33f1` explicitly disabled `-e` around that expected-to-fail pipeline and added a dedicated reporter.

**Run `40640682878`, commit `c9d33f1`:** the ordinary direct macOS acceptance again exited `-11` during the dock request. The corrected LLDB-launched retry completed, opened the dock and Inspect successfully, and produced no crash backtrace; Windows geometry/runtime and UI/security passed. This confirms the debugger-launched retry changes the failure behavior and is not a product fix.

**Run `40642767377`, commit `9d5c191`:** direct macOS acceptance exited `-10` during the dock request. The late-attach diagnostic retry did not reach its attach/resume marker within 15 seconds; the only captured LLDB output was `(lldb) process attach --pid 11888`, so no stack was obtained. This diagnoses a failure in the attach instrumentation, not the product crash.

**Run `40644209434`, commit `01308fc`:** the macOS direct test issued the DevTools dock request, then lost its CDP connection (`Runtime.evaluate ... Connection reset by peer`); no faulting stack was collected. The LLDB retry remained stuck after `(lldb) process attach --pid 26828`; adding non-interactive `sudo` did not make it ready. The Windows acceptance separately exited with `3221225477` (`0xC0000005`) after the CI markers confirmed `OnBeforeDevToolsPopup`, `AddOverlayView`, overlay bounds, and overlay attachment had completed. These observations narrow the failure window but do not establish a root cause. UI/security regression tests passed.

**Run `40646404915`, commit `102f68e`:** all jobs passed, including direct macOS DevTools geometry and Windows geometry. The LLDB/sample retry was skipped because direct acceptance passed, so this is a clean acceptance result but does not exercise the new diagnostic path or explain the earlier intermittent crash.

**Run `40648434552`, commit `ad4d8bd`:** direct macOS geometry/DevTools and its following runtime acceptance passed again. Windows geometry failed with `3221225477` (`0xC0000005`) after the callback, overlay bounds, and overlay-attached markers; Windows runtime acceptance was skipped. UI/security tests passed. Thus two consecutive macOS direct attempts passed, while the Windows native failure remained intermittent. The macOS LLDB/sample retry did not run.

**Run `40650834745`, commit `efa62fb`:** macOS direct acceptance reproduced status `-10` while waiting for `ShowDevTools` to return. The revised retry did reissue the DevTools request and reproduced status `-11`; Windows and UI/security passed. LLDB still produced no attach output/backtrace. The sample utility reported `Sampling process 0 for 5 seconds` and produced no sample report, so no stack was obtained. This run tests the non-blocking retry but does not establish the product cause.

**Run `40652554592`, commit `c414f81`:** macOS direct acceptance exited `-11`; the retry exited `-5` while waiting for the same `ShowDevTools` call. LLDB again produced no attach output. The sample command log confirmed target PID `29211`, ran `/usr/bin/sample 29211 5`, and exited `0`, but the output report remained empty (`report_bytes=0`). Windows and UI/security passed. No faulting stack was obtained.

**Run `40654197412`, commit `bdad27e`:** macOS direct DevTools geometry and runtime acceptance passed, so the LLDB/sample retry did not run. Windows geometry again exited `3221225477` (`0xC0000005`); the UI/security suite passed. The new `sample -file -mayDie` command therefore remained untested in that run.

**Run `40656760526`, commit `7ee14c3`:** direct macOS acceptance reproduced status `-10`; the retry reissued the dock and Inspect requests and both returned successfully, so the retry did not reproduce the crash. LLDB remained pending after `(lldb) process attach --pid 34605`. The `sample -mayDie -file` command then exceeded its 7-second budget and was terminated with exit `-15`, producing a zero-byte report. The direct/retry outcome again shows timing sensitivity, but supplies no faulting stack and does not prove a product cause.

**Current diagnostic status:** the retry waits two seconds for LLDB and then proceeds with the same DevTools reproduction; it also sets `TERM=dumb`. In `40656760526`, the sample ran concurrently with the still-pending LLDB attach and stalled, while the DevTools retry passed. The worktree now stops a pending LLDB process before using `sample`, so the two capture tools do not compete. A GitHub `workflow_dispatch` attempt returned HTTP 403 (`Resource not accessible by integration`); a one-shot `[native-devtools-diag]` push marker now provides an opt-in retry when direct acceptance passes. This remains diagnostic-only; no product code was changed. Local tests pass. Exact native cause and product fix remain unresolved.

## Решение о возврате пользовательских entry points как заглушек (2026-10-28)
- «Инструменты разработчика» и «Инспектировать» возвращены только как локальная UI-заглушка; нативный frontend, webview и DevTools IPC не возвращались.
- Нативные вызовы `ShowDevTools`/`CloseDevTools` остаются удалёнными после воспроизводимых падений: macOS `SIGSEGV`, Windows `0xC0000005`.
- Точная причина crash не найдена; внутренний CDP для snapshot, cache/storage и CI automation сохранён.
