# SHELTER — приватный браузер на Chromium Embedded Framework

Десктопная оболочка для дизайна SHELTER (`resources/ui/index.html`). Единственный источник версии сборки — `SHELTER_VERSION` в `CMakeLists.txt`.
Платформы: **Windows x64** и **macOS Intel (x86_64)**. Сборка использует CEF 154.0.32 (Chromium 154), а двоичные архивы закреплены по SHA-256. Статус отставания и процедура обновления: [`docs/cef-maintenance.md`](docs/cef-maintenance.md).

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
для Windows, `SHELTER-macos-x64-<версия>.dmg` для macOS (суффикс берётся из
`SHELTER_VERSION` в CMake). Версия для UI, ресурсов Windows, установщика и упаковки
генерируется из этого единственного значения. Целевой release workflow публикует `SHA256SUMS.txt`, detached-подпись `SHA256SUMS.txt.minisig` и GitHub build-provenance attestations. Сейчас публичный ключ placeholder, а GitHub Environment approval/tag controls не подтверждены; workflow fail-closed на placeholder/отсутствующих secrets, но одно объявление Environment в YAML не доказывает наличие reviewer approval. Не выпускайте релиз, пока владелец не завершит [`docs/release-key.md`](docs/release-key.md) и не проверит настройки GitHub.

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

По целевому workflow готовые установщики публикуются в **Releases** после успешных Windows/macOS сборок, тестов, допуска через Environment `release-signing`, подписи манифеста и проверки attestation. Тег `v*` должен совпадать с `SHELTER_VERSION` в `CMakeLists.txt`. В текущем репозитории публичный ключ всё ещё placeholder, а reviewer/tag controls Environment не подтверждены: job должен остановиться на placeholder/отсутствующих secrets. Владелец обязан вручную проверить эти controls до добавления secrets и первого релиза.

- **Windows x64** — `SHELTER-Setup-x64.exe`. Ставится на пользователя без прав администратора в `%LOCALAPPDATA%\Programs\SHELTER`, создаёт ярлык в меню «Пуск» (и на рабочем столе по желанию), удаляется через «Приложения и возможности». Профиль браузера при удалении сохраняется. Также есть `SHELTER-windows-x64-portable.zip`. Authenticode-подписи нет, поэтому Windows SmartScreen может показать предупреждение.
- **macOS (Intel)** — `SHELTER-macos-x64-<версия>.dmg` (версия из `CMakeLists.txt`): откройте образ и перетащите SHELTER в Applications. Приложение подписано ad-hoc (без Developer ID и нотаризации), поэтому Gatekeeper может показать предупреждение. Это предупреждение не устранено: сначала проверьте minisign и SHA-256 по инструкциям ниже.

CI проверяет оба установщика: устанавливает их так же, как пользователь, запускает браузер из места установки и прогоняет smoke-тест (на Windows — ещё и удаление).

## Проверка целостности релиза

До запуска скачивайте артефакты только со страницы Releases репозитория `Everick-Ven/shelter`. Сверьте fingerprint minisign public key из `docs/release-pubkey.txt` с fingerprint, опубликованным владельцем **по отдельному доверенному каналу**. Не считайте fingerprint, взятый только с той же страницы релиза, независимым подтверждением. Сейчас ключ в репозитории — placeholder: пока владелец не заменит его и не опубликует fingerprint, релизное подписание заблокировано; не запускайте артефакты как будто они проверены.

Скачайте в один каталог `SHA256SUMS.txt`, `SHA256SUMS.txt.minisig`, public-key файл и нужные установщик/архив. Установите minisign из доверенного источника, затем сначала проверьте подпись манифеста:

```sh
minisign -V -p release-pubkey.txt -m SHA256SUMS.txt -x SHA256SUMS.txt.minisig
```

Только если команда сообщает об успешной проверке, сверьте файлы с манифестом (имена в нём — basename артефактов):

```sh
# macOS / Linux: в каталоге со всеми перечисленными файлами
shasum -a 256 -c SHA256SUMS.txt
```

В Windows PowerShell получите SHA-256 каждого файла из манифеста, например:

```powershell
Get-FileHash .\\SHELTER-Setup-x64.exe -Algorithm SHA256
Get-FileHash .\\SHELTER-windows-x64-portable.zip -Algorithm SHA256
```

Сравните `Hash` со строкой соответствующего имени в `SHA256SUMS.txt`; для DMG используйте `shasum -a 256`. Дополнительно проверьте provenance:

```sh
gh attestation verify ./SHELTER-Setup-x64.exe -R Everick-Ven/shelter
```

Повторите проверку attestation для каждого скачанного артефакта. Подпись манифеста, совпавший checksum и attestation не устраняют предупреждения Gatekeeper/SmartScreen и не заменяют анализ доверия к издателю. Не обходите эти предупреждения до проверки происхождения релиза.

## Данные

| ОС | Каталог |
|----|---------|
| Windows | `%LOCALAPPDATA%\SHELTER` |
| macOS | `~/Library/Application Support/SHELTER` |

Инвентаризация локальных данных, остаточных рисков, текущих сетевых функций и статус проверок — в [`docs/security-data-audit.md`](docs/security-data-audit.md). Не считайте весь профиль CEF зашифрованным; режим «Призрак» не удаляет скачанные файлы и не гарантирует secure erase старых значений Local Storage.

## Состояние (этап 1, MVP)

Есть: окно, UI, вкладки/пространства с изоляцией сессий, навигация (адрес, назад/вперёд/обновить/стоп),
popup → новая вкладка, страница ошибки загрузки, загрузки с подтверждением, контекстное меню страниц
(через HTML-меню UI), буфер обмена, зум, печать, «Удалить данные».

Также есть: поиск по странице на нативных страницах (`CefFindHandler`, панель поиска UI сдвигает вид вкладки
вниз), нативное шифрование секретов (`secretEnc/secretDec`: ChaCha20 + HMAC-SHA256) в browser process;
мастер-ключ не передаётся renderer’у. На Windows ключ защищён DPAPI; на macOS он хранится в Keychain и
зеркалируется в `secret.key` с правами 0600, чтобы смена ad-hoc подписи при обновлении не лишала доступа.
Зеркало защищено правами текущего пользователя, но не изолирует ключ от других процессов того же пользователя.

Блокировка трекеров и рекламы работает на сетевом уровне (отменяются рекламные и трекерные запросы,
включая подресурсы главного фрейма — Yandex RTB/AdFox, googletagservices, SSP-биржи) и косметически
(скрытие рекламных контейнеров). Автосогласие cookies подтверждает минимальный (технический) набор
до первого кадра баннера: окно согласия не показывается, поддержаны известные CMP, включая shadow DOM.

Состояние тумблеров защит доставляется в каждый процесс рендерера: при рождении — командной строкой
(`--shelter-protections`), а при навигации — сообщением, поэтому включённые защиты действуют и на тех
страницах, что открыты уже после переключения. «Запасной» процесс рендерера
(`SpareRendererForSitePerProcess`) отключён: он рождается с одним состоянием тумблеров и может получить
страницу уже после их переключения — то есть применить устаревшее.

Расширения: распакованные пакеты (Chrome Web Store по ID, файл .crx/.zip) лежат рядом с данными —
`<данные>/Extensions/<id>`; та же раскладка используется для `--load-extension`. Пакеты с manifest v3
отдаются движку Chromium (CEF вырезал embedder-API расширений, поэтому применяется штатный аргумент
Chromium: content-scripts и фон грузит движок), manifest v2 грузит политика Chromium запрещает — такие
пакеты оболочка обслуживает сама, внедряя объявленные content-scripts (JS/CSS) по правилам манифеста
(`matches`/`exclude_matches`/`include_globs`/`exclude_globs`/`all_frames`). Факт загрузки движком не
предполагается, а проверяется запросом к самому расширению (`chrome-extension://<id>/manifest.json`):
статус в интерфейсе отражает ответ движка, а не факт передачи аргумента. Список путей фиксируется при
запуске, поэтому новое расширение подхватывается после перезапуска — интерфейс это сообщает.

Пока нет: окно входа (`authWindow` — вёрстка использует собственный запасной путь), встроенная панель
DevTools, миниатюры вкладок в реальном времени, подпись/нотаризация, полная модель расширений Chrome
(фон/service worker для MV2, chrome.* API, страницы настроек расширений).
