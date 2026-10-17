# План подписания релизов SHELTER

> **Статус:** проектирование. Этот документ описывает будущий процесс; он не меняет workflow, не подписывает текущие артефакты и не включает автоматическое обновление CEF.

## Текущее состояние

- Версия CEF закреплена в `CMakeLists.txt` параметром `CEF_VERSION`; ключ кеша CEF задаётся отдельно в `.github/workflows/build.yml` через `CEF_VERSION_KEY`.
- Windows job выпускает Inno Setup (`SHELTER-Setup-x64.exe`) и portable ZIP.
- macOS job собирает DMG. Сейчас `tools/make_dmg.sh` подписывает вложенные компоненты и приложение ad-hoc, без Developer ID и нотаризации.
- Release job создаёт `SHA256SUMS.txt`, но не подписывает сам манифест. GitHub Actions использует стандартный `GITHUB_TOKEN`; сертификаты подписи и секреты Apple пока не настроены.

## Целевой процесс релиза

1. Сборки, тесты и smoke/acceptance-проверки выполняются без ключей подписи. Pull request и сборки из fork никогда не получают release secrets.
2. Для защищённого тега `v*` артефакты передаются в платформенные signing jobs. Эти jobs запускаются в защищённом GitHub Environment `release-signing` с обязательным reviewer approval.
3. Windows Setup и portable ZIP создаются из уже подписанных PE-файлов. macOS DMG создаётся из подписанного и прошедшего нотаризацию `SHELTER.app`.
4. После окончательной подписи и упаковки формируются контрольные суммы всех публикуемых артефактов. Затем detached-подписью подписывается `SHA256SUMS.txt`; перед публикацией выполняется повторная проверка манифеста и его подписи.
5. В GitHub Release загружаются только проверенные артефакты, `SHA256SUMS.txt` и его подпись. В release notes указываются идентификатор сертификата/публичного ключа и команды проверки.

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

Предлагаемый формат — detached-подпись **minisign**:

- Публичный ключ проверки публикуется в репозитории или на официальной странице SHELTER и его fingerprint указывается в release notes.
- Приватный ключ хранится только в GitHub Environment `release-signing`; workflow подписывает готовый `SHA256SUMS.txt` и прикладывает рядом `SHA256SUMS.txt.minisig`.
- Release job проверяет подпись закреплённым публичным ключом до создания/обновления GitHub Release. Проверка SHA-256 выполняется по именам фактически загружаемых файлов.
- Манифест строится **после** Authenticode, codesign, notarization/stapling и окончательной упаковки. Сам файл подписи не включается в собственный манифест.

## Планируемые GitHub Actions secrets

Создать Environment `release-signing`, ограничить его защищёнными release tags и включить обязательное ручное подтверждение. Использовать отдельные, минимально доступные секреты:

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

## Планируемые изменения workflow

Изменения вносятся только после утверждения этого плана:

1. Оставить обычные Windows/macOS build и тестовые jobs без signing credentials.
2. Для release tag включить отдельные Windows/macOS signing jobs под защищённым Environment. Они скачивают проверенные build artifacts, подписывают PE или `.app`, создают соответствующий пакет и публикуют его как промежуточный artifact.
3. Добавить проверки Authenticode, `codesign`, notarization/staple и Gatekeeper до загрузки артефактов в release. Любая ошибка подписи должна завершать job с ненулевым кодом.
4. В release job скачать только подписанные артефакты, создать `SHA256SUMS.txt`, подписать его minisign-ключом, проверить подпись и контрольные суммы, затем загрузить файлы в GitHub Release.
5. Не запускать шаги, использующие signing secrets, на `pull_request`, включая fork PR. Не передавать секреты в сторонние actions; использовать встроенные системные инструменты и pinned actions.

## Регулярное обновление CEF

- Проверять стабильные CEF/Chromium обновления **ежеквартально**, а также вне графика при исправлениях критических CVE. Перед каждым релизом проверять, нет ли более свежего security build для текущей ветки.
- Обновлять точную версию CEF в `CMakeLists.txt` и соответствующий `CEF_VERSION_KEY` в workflow одним reviewable PR. Зафиксировать исходный выпуск, платформенные архивы и опубликованные CEF checksums; не переходить на непроверенный канал или плавающий URL.
- Для PR выполнить Windows x64 и macOS Intel сборки, sandbox checks, CRX3 и security regression tests, CI-hook acceptance, а также проверить подписи/загрузку CEF helper-компонентов. При обновлении CEF повторно проверить список требуемых entitlements и вложенную структуру подписываемого `.app`.
- В release notes указывать версию CEF/Chromium и существенные security updates. Если обновление откатили, закрепить предыдущую проверенную версию и не менять release signing keys в том же изменении.

## Проверка пользователем релиза

Для Windows проверить Publisher и цепочку подписи Setup и `Shelter.exe` в свойствах файла или через SignTool. Для macOS проверить `codesign --verify`, Gatekeeper assessment и stapled ticket на чистой машине. Для всех платформ проверить detached-подпись `SHA256SUMS.txt`, затем сравнить SHA-256 скачанных Setup, ZIP и DMG с манифестом. Проверять нужно скачанные файлы, а не только локальные build outputs.
