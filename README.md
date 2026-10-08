# SHELTER — приватный браузер на Chromium Embedded Framework

Десктопная оболочка для дизайна SHELTER (`resources/ui/index.html`, версия 1.0.165).
Платформы: **Windows x64** и **macOS Intel (x86_64)**. CEF 154.0.32 (Chromium 154).

## Как это устроено

* **C++ + CEF Views.** Одно фреймлесс-окно. Интерфейс (`index.html`) — отдельный `CefBrowser`,
  каждая вкладка — собственный `CefBrowserView` (overlay поверх области `#viewport`).
* **UI загружается с собственной схемы** `shelter://app/index.html` (ресурсы из папки `ui/` рядом с
  приложением). Схема отдаётся только UI-браузеру, страницы из вкладок к ней не привязаны.
* **Мост `window.shelterNative`** (`resources/ui/host-bridge.js`) работает поверх `cefQuery`;
  принимает вызовы только от UI-браузера со страницы `shelter://app/`.
* **Изоляция сессий:** пространство = `persist:space-<key>` → отдельный `CefRequestContext`
  с каталогом профиля; режим «Призрак» = `temp:ghost-<key>` → контекст только в памяти.
* Пока поверх страницы открыто HTML-меню, модальное окно или тост, нативный вид вкладки
  заменяется снимком (`Page.captureScreenshot`), иначе HTML оказался бы под нативным видом.

## Сборка

Локально ничего ставить не нужно — сборки запускает GitHub Actions (`.github/workflows/build.yml`).
Финальные файлы в Releases: `SHELTER-Setup-x64.exe` и `SHELTER-windows-x64-portable.zip`
для Windows, `SHELTER-macos-x64.dmg` для macOS. Рядом публикуется `SHA256SUMS.txt`.

Артефакты вкладки Actions — это ZIP-обёртки GitHub: `SHELTER-windows-x64.zip` содержит
Windows-файлы сборки, а `SHELTER-macos-x64.zip` содержит DMG и контрольную сумму. На macOS
извлекайте и проверяйте `.dmg`; отдельный macOS ZIP-пакет проект не выпускает.

Вручную (нужны CMake ≥ 3.21 и VS 2022 / Xcode CLT):

```bash
# Windows
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DUSE_SANDBOX=ON
cmake --build build --config Release --target Shelter
# macOS (Intel)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPROJECT_ARCH=x86_64 -DUSE_SANDBOX=ON
cmake --build build --target Shelter
```

CEF скачивается автоматически (`cmake/DownloadCEF.cmake`, минимальный дистрибутив).
Релизные сборки должны оставаться с `USE_SANDBOX=ON`: Windows использует CEF bootstrap
(`Shelter.exe` + `Shelter.dll`), macOS — `libcef_sandbox.dylib` и sandbox context в helper-процессах.
Отключение sandbox допустимо только для локальной отладки, не для распространения.

## Установка

Готовые установщики лежат на странице **Releases** репозитория (создаются автоматически при пуше тега `v*`, например `git tag v1.0.165 && git push origin v1.0.165`), а также в артефактах каждого прогона Actions.

- **Windows x64** — `SHELTER-Setup-x64.exe`. Ставится на пользователя без прав администратора в `%LOCALAPPDATA%\Programs\SHELTER`, создаёт ярлык в меню «Пуск» (и на рабочем столе по желанию), удаляется через «Приложения и возможности». Профиль браузера при удалении сохраняется. Также есть `SHELTER-windows-x64-portable.zip`.
- **macOS (Intel)** — `SHELTER-macos-x64.dmg`: откройте образ и перетащите SHELTER в Applications. Приложение подписано ad-hoc (без Developer ID и нотаризации), поэтому при первом запуске Gatekeeper может ругаться: ПКМ → «Открыть» либо `xattr -dr com.apple.quarantine /Applications/SHELTER.app`.

CI проверяет оба установщика: устанавливает их так же, как пользователь, запускает браузер из места установки и прогоняет smoke-тест (на Windows — ещё и удаление).

## Данные

| ОС | Каталог |
|----|---------|
| Windows | `%LOCALAPPDATA%\SHELTER` |
| macOS | `~/Library/Application Support/SHELTER` |

## Состояние (этап 1, MVP)

Есть: окно, UI, вкладки/пространства с изоляцией сессий, навигация (адрес, назад/вперёд/обновить/стоп),
popup → новая вкладка, страница ошибки загрузки, загрузки с подтверждением, контекстное меню страниц
(через HTML-меню UI), буфер обмена, зум, печать, F12 → DevTools в отдельном окне, «Удалить данные».

Также есть: поиск по странице на нативных страницах (`CefFindHandler`, панель поиска UI сдвигает вид вкладки
вниз), шифрование секретов UI (`secretEnc/secretDec`: ChaCha20 + HMAC-SHA256, мастер-ключ — DPAPI на
Windows, файл `secret.key` с правами 0600 на macOS).

Блокировка трекеров и рекламы работает на сетевом уровне (отменяются рекламные и трекерные запросы,
включая подресурсы главного фрейма — Yandex RTB/AdFox, googletagservices, SSP-биржи) и косметически
(скрытие рекламных контейнеров). Автосогласие cookies подтверждает минимальный (технический) набор
до первого кадра баннера: окно согласия не показывается, поддержаны известные CMP, включая shadow DOM.

Состояние тумблеров защит доставляется в каждый процесс рендерера: при рождении — командной строкой
(`--shelter-protections`), а при навигации — сообщением, поэтому включённые защиты действуют и на тех
страницах, что открыты уже после переключения. «Запасной» процесс рендерера
(`SpareRendererForSitePerProcess`) отключён: он рождается с одним состоянием тумблеров и может получить
страницу уже после их переключения — то есть применить устаревшее.

Пока нет: окно входа (`authWindow` — вёрстка использует собственный запасной путь), встроенная панель
DevTools, миниатюры вкладок в реальном времени, подпись/нотаризация.
