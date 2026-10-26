# План подписания релизов SHELTER

> **Статус на 2026-10-25:** P0 workflow внедрён частично в `.github/workflows/build.yml`: после owner setup он создаёт SHA-256 manifest, detached minisign signature, проверяет её, выпускает GitHub provenance attestations; CEF архивы закреплены по SHA-256. Public key сейчас placeholder, secrets не настроены/не проверены, GitHub Environment controls не подтверждены; текущая job должна остановиться на placeholder/отсутствующих secrets. Объявление `environment: release-signing` в YAML само по себе не включает reviewers, tag rules или branch/tag protection. Реальный подписанный релиз не выпускался. Authenticode, Developer ID и notarization не внедрены; Gatekeeper/SmartScreen предупреждения остаются ожидаемыми.

## Текущее состояние

- Версия и ожидаемые SHA-256 Windows/macOS архивов CEF закреплены в `CMakeLists.txt`; `cmake/DownloadCEF.cmake` проверяет их при скачивании и перед использованием кешированного архива. Текущий отрыв от stable и источники зафиксированы в [`cef-maintenance.md`](cef-maintenance.md).
- Windows job выпускает Inno Setup (`SHELTER-Setup-x64.exe`) и portable ZIP. macOS job собирает DMG; `tools/make_dmg.sh` подписывает вложенные компоненты ad-hoc, без Developer ID и нотаризации.
- Tag-only release job под Environment `release-signing` создаёт SHA-256 manifest, подписывает его minisign 0.12 и проверяет подпись public key, загруженным из `main` (не из tag commit). Затем provenance action attests установщик, ZIP, DMG, manifest и `.minisig`; только после этого `gh release create` публикует assets. Защита самого workflow/tag по-прежнему зависит от GitHub settings и review владельца.
- `docs/release-pubkey.txt` пока placeholder; `release-signing` Environment, required reviewers/tag rules и 2FA/security settings не подтверждены. Job fail-closed на placeholder/отсутствующих secrets, но workflow не может проверить наличие reviewer/tag controls. Не считать YAML Environment реальной ручной блокировкой, пока настройки не проверены в GitHub UI.
- Для текущего P0 workflow нужны только `RELEASE_MINISIGN_SECRET_KEY_BASE64` и `RELEASE_MINISIGN_PASSWORD` как Environment secrets. Сертификаты Authenticode/Apple не настроены и не используются.

## Целевой процесс релиза

1. **Реализовано в P0:** сборки и тесты выполняются без minisign secrets; release job запускается только для `v*` в официальном репозитории и объявляет Environment `release-signing`. Environment approval/tag protection остаются owner action и ещё не проверены. PR/fork jobs не ссылаются на signing secrets.
2. **Реализовано в P0:** из готовых Windows/macOS артефактов строится SHA-256 manifest; он подписывается detached minisign 0.12 из digest-pinned source archive и проверяется repository public key до публикации.
3. **Реализовано в P0:** provenance action attests все финальные release assets после подписи манифеста; release notes содержат команды проверки. SHA-256 `.minisig` не включается в собственный manifest.
4. **Не реализовано (отдельная P1):** Authenticode, Developer ID, hardened runtime/notarization и платформенные signing jobs. Текущие installers/DMG не становятся подписанными этими сертификатами из-за мини-signature.
5. Владелец обязан сначала заменить placeholder public key, независимо опубликовать fingerprint и подтвердить GitHub Environment, reviewer/tag rules и secrets. Workflow блокируется кодом на placeholder/отсутствующих secrets, но не может самостоятельно подтвердить настройки reviewer/tag; до ручной проверки реальный release tag не создавать.

## Windows — Authenticode

### Что подписывать и в каком порядке

- Подписывать принадлежащие SHELTER PE-файлы, прежде всего `Shelter.dll` и `Shelter.exe` (CEF bootstrap), **после** всех изменений ресурсов версии и до упаковки portable ZIP и Inno Setup.
- Подписывать собранный `SHELTER-Setup-x64.exe` после компиляции Inno Setup. Внутренние файлы установщика должны уже иметь действительные подписи.
- Не переподписывать сторонние CEF-файлы: сохранять их upstream-подписи и отдельно проверять их целостность.
- Portable ZIP как ZIP-контейнер не имеет Authenticode-подписи. Его содержимое подписывается до архивирования, а сам ZIP включается в подписанный `SHA256SUMS.txt`.

### Операции будущего workflow

1. На Windows runner установить код-сертификат в краткоживущий временный контейнер из PFX, не печатая PFX, пароль и приватный ключ в лог.
2. Для каждого app-owned PE применить `signtool sign` с digest SHA-256 и RFC 3161 timestamp. Сервер timestamp будет фиксированной конфигурацией workflow, а не секретом.
3. Проверить подпись каждого PE и Setup через `signtool verify /pa /all /v`; дополнительно проверить ожидаемое имя издателя, цепочку сертификата, timestamp и период действия.
4. Только после успешной проверки собрать portable ZIP и Setup. После упаковки установить Setup во временный каталог и проверить подписи извлечённых app-owned PE-файлов.
5. Удалить временный сертификат и файлы PFX даже при ошибке job.

## macOS — Developer ID и нотариализация

### Подпись приложения

- Использовать сертификат **Developer ID Application** с hardened runtime и secure timestamp. Для DMG отдельный Developer ID Installer не нужен; он понадобится только при выборе формата PKG.
- Подписывать вложенные Mach-O и bundle-компоненты CEF изнутри наружу: dylib/вспомогательные компоненты, CEF framework, CEF helper apps, затем `SHELTER.app`. Не применять `codesign --deep` для подписи: этот флаг допустим только как дополнительная проверка.
- При реализации заменить текущие ad-hoc `codesign --sign -` шаги в `tools/make_dmg.sh` на Developer ID-подпись; нельзя сначала подписать Developer ID, а затем повторно переподписать bundle ad-hoc, так как это инвалидирует подпись.
- Перед внедрением проверить, какие entitlements действительно нужны CEF sandbox helper-процессам. Использовать минимальный зафиксированный plist; не отключать hardened runtime и не добавлять широкие entitlements «на всякий случай».
- После подписи выполнить `codesign --verify --deep --strict --verbose=2` для приложения и проверку Gatekeeper assessment.

### Нотаризация DMG

1. Создать DMG из финального подписанного приложения.
2. Отправить DMG через `xcrun notarytool submit --wait`, используя App Store Connect API key; не использовать интерактивный Apple ID или app-specific password в CI.
3. При успехе прикрепить ticket командой `xcrun stapler staple`, затем проверить `xcrun stapler validate` и повторно проверить приложение из смонтированного DMG.
4. Только после staple/verification вычислить SHA-256 DMG. Ошибка notarization или staple блокирует публикацию.

## Подпись `SHA256SUMS.txt`

Формат detached-подписи **minisign 0.12** реализуется в текущем P0 workflow:

- Workflow загружает официальный source archive tag 0.12, проверяет его SHA-256 `796dce1376f9bcb1a19ece729c075c47054364355fe0c0c1ebe5104d508c7db0` и собирает локальный verifier/signing tool из этого source. Linux release binary и его digest не используются.
- Публичный ключ после owner action будет в `docs/release-pubkey.txt`; release notes печатают SHA-256 fingerprint decoded public-key record. Fingerprint нужно отдельно сообщить/проверить по доверенному каналу. Сейчас файл placeholder и реального fingerprint нет.
- Временный private key декодируется только внутри release job из Environment secret, с `umask 077`, подписывает финальный `SHA256SUMS.txt`, затем подпись сразу проверяется и временный key file удаляется trap-ом. Пароль миниsign получает через stdin, не через CLI argument. Пользовательские key/password secrets не коммитятся.
- Release job проверяет detached signature до создания GitHub Release. Проверка SHA-256 выполняется по basename трёх финальных файлов; manifest и signature также attest-ятся.
- Сам `.minisig` не включается в подписываемый manifest. P1 Authenticode/codesign/notarization пока нет; когда появятся, manifest должен формироваться после их применения и окончательной упаковки.

## GitHub Actions secrets и настройки Environment

Только два секрета `RELEASE_MINISIGN_SECRET_KEY_BASE64` и `RELEASE_MINISIGN_PASSWORD` используются текущим P0 workflow. Сначала владелец должен офлайн создать ключ по [`release-key.md`](release-key.md), заменить placeholder, а затем вручную проверить/настроить `release-signing` Environment, required reviewers, deployment tag rule `v*` и repository tag rules. Эти GitHub settings в ходе аудита не менялись и пока не подтверждены.

Остальные строки таблицы — только возможные secrets для P1 platform signing; их **не создавать** до отдельного решения владельца:

| Secret | Назначение |
|---|---|
| `WINDOWS_CODESIGN_PFX_BASE64` | PFX-сертификат Authenticode в base64 |
| `WINDOWS_CODESIGN_PFX_PASSWORD` | Пароль PFX |
| `APPLE_DEVELOPER_ID_P12_BASE64` | Экспортированный Developer ID Application сертификат и приватный ключ |
| `APPLE_DEVELOPER_ID_P12_PASSWORD` | Пароль экспортированного `.p12` |
| `APPLE_TEAM_ID` | Team ID для codesign/notarytool |
| `APPLE_NOTARY_KEY_P8_BASE64` | App Store Connect API key в base64 |
| `APPLE_NOTARY_KEY_ID` | Key ID для notarytool |
| `APPLE_NOTARY_ISSUER_ID` | Issuer ID для notarytool |
| `RELEASE_MINISIGN_SECRET_KEY_BASE64` | Приватный minisign key |
| `RELEASE_MINISIGN_PASSWORD` | Пароль приватного minisign key |

Имена издателя, отпечаток ожидаемого сертификата, minisign public key, пути к entitlements и timestamp URL не являются секретами — их следует закрепить и проверять в репозитории. Стандартный `GITHUB_TOKEN` остаётся ограниченным правом `contents: write` только для release job. Ротация или отзыв сертификата/ключа оформляется отдельной операцией с обновлением fingerprint и публичного ключа.

## Будущие изменения workflow: P1 platform signing

Это отдельный этап, не реализуемый до решений владельца. Текущий P0 detached-manifest-signing уже описан выше.

1. Оставить обычные Windows/macOS build и тестовые jobs без platform-signing credentials.
2. После отдельного утверждения включить Windows/macOS signing jobs только в теговый release path под reviewer-protected Environment. Они должны брать проверенные build artifacts, подписывать app-owned PE или `.app`, создавать соответствующий пакет и передавать его как финальный artifact.
3. Добавить проверки Authenticode, `codesign`, notarization/staple и Gatekeeper до включения файлов в release manifest. Любая ошибка подписи завершает job.
4. Оставить minisign manifest signing/provenance после финальной упаковки и пересчитывать manifest после новых платформенных подписей.
5. Не запускать шаги с platform-signing secrets на `pull_request`, включая fork PR; не передавать secrets сторонним actions; использовать системные инструменты и pinned actions.

## Регулярное обновление CEF

- Проверять стабильные CEF/Chromium обновления **ежеквартально**, а также вне графика при исправлениях критических CVE. Перед каждым релизом проверять, нет ли более свежего security build для текущей ветки.
- Обновлять точную версию CEF в `CMakeLists.txt` и соответствующий `CEF_VERSION_KEY` в workflow одним reviewable PR. Зафиксировать исходный выпуск, платформенные архивы и опубликованные CEF checksums; не переходить на непроверенный канал или плавающий URL.
- Для PR выполнить Windows x64 и macOS Intel сборки, sandbox checks, CRX3 и security regression tests, CI-hook acceptance, а также проверить подписи/загрузку CEF helper-компонентов. При обновлении CEF повторно проверить список требуемых entitlements и вложенную структуру подписываемого `.app`.
- В release notes указывать версию CEF/Chromium и существенные security updates. Если обновление откатили, закрепить предыдущую проверенную версию и не менять release signing keys в том же изменении.

## Проверка пользователем релиза

Для Windows проверить Publisher и цепочку подписи Setup и `Shelter.exe` в свойствах файла или через SignTool. Для macOS проверить `codesign --verify`, Gatekeeper assessment и stapled ticket на чистой машине. Для всех платформ проверить detached-подпись `SHA256SUMS.txt`, затем сравнить SHA-256 скачанных Setup, ZIP и DMG с манифестом. Проверять нужно скачанные файлы, а не только локальные build outputs.
