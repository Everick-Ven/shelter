# P0: аудит релизов, хранения и сетевых границ

**Дата среза:** 2026-10-25. **Тип проверки:** чтение исходников, workflow и документации; не runtime-аудит Windows/macOS. Тестовые результаты перечислены в конце и обновляются после выполнения. CEF build и release-signing job в этой среде не запускались.

> **Границы аудита:** отчёт отражает исходники на существующем tip ветки `arena/dc997a8d-shelter` плюс изменения этой P0-фазы. В базовой ветке уже присутствуют planner `ss1` и опциональный сетевой AI-провайдер; эта P0 не добавляет сетевых endpoint/telemetry и не меняет формат `ss1`. Закрытое P0-изменение касается только безопасного хранения planner checkpoints через уже существующий `ss1` и запрета plaintext fallback. Упоминание AI-трафика — инвентаризация существующей функции, а не её одобрение или новая реализация; её соответствие продуктовой политике остаётся решением владельца.

## Итог

- В workflow добавлена проверка целостности релиза: SHA-256 manifest, detached minisign-подпись, её немедленная проверка закреплённым ключом, GitHub build-provenance attestation и публикация только после всех шагов. Actions закреплены полными commit SHA; ключ создаёт владелец офлайн.
- **Релиз пока не подписывается:** `docs/release-pubkey.txt` — placeholder, Environment `release-signing` и его reviewer/tag controls не подтверждены. Workflow должен завершиться ошибкой из-за placeholder/отсутствующих secrets. Упоминание Environment в YAML само по себе не создаёт reviewer approval или tag protection.
- CEF закреплён на `154.0.32+g682c378+chromium-154.0.8037.58`. Официальный CEF index на дату аудита показывал stable `156.0.14+g4fa4ab5+chromium-156.0.8078.48`. Это отставание на две major-версии Chromium; оно **не** доказывает наличие конкретного CVE и не заменяет per-CVE оценку.
- Новые planner migration/pre-import/unreadable-store backups используют `ss1:` и проверку записи; при ошибках задача остаётся read-only. Старые plaintext backups и LevelDB remnants не инвентаризированы физически и не удалялись.
- Ghost transition при включении теперь помечает открытую вкладку приватной, немедленно перерисовывает view и не сохраняет ghost-вкладки в `shelter:spaces`; вкладки, впервые открытые во время режима, получают тот же флаг. Добавлен source-level regression test. CEF runtime/cookie-переход на обеих ОС не проверен.
- Остаточные вопросы требуют решения владельца: macOS `secret.key` — plaintext mirror master key с правами 0600; native decryptor в текущем исходнике не поддерживает чтение `p1:`; старые Local Storage/LevelDB копии и browsing profile не имеют доказанного secure erase/full-profile encryption.

## Данные на устройстве

| Область | Где / формат в текущем коде | Остаточные ограничения |
|---|---|---|
| Открытые вкладки и пространства | `shelter:spaces` — обычный JSON: URL, заголовки, stack/back-forward state, названия пространств, IDs/флаги вкладок | Это plaintext app metadata. Ghost-фильтр исключает вкладки, отмеченные `ghost`; включение режима теперь помечает текущую вкладку до `saveNow()`. Перезапись Local Storage не стирает старые WAL/SST версии. |
| Закладки и небольшие настройки | `shelter:bookmarks`, `shelter:theme`, `shelter:ghost`, `shelter:searchEngine`, `shelter:currentSpace`, `shelter:activeTabId`, `shelter:ext`, `shelter:favcache` | Хранятся в открытом виде. URL в закладках и кэшированные favicon могут раскрывать сайты. |
| История посещений | `shelter:visits`; новые значения проходят через `ss1:` | Legacy plaintext читается и будет зашифрован при последующем сохранении; резервные старые копии в LevelDB не очищаются доказанным способом. |
| Планировщик | `shelter:board` и `shelter:boards` записываются как `ss1:`; старые plaintext значения по-прежнему читаются | `shelter:eisenTasks`/`eisenTasks` — legacy источники до проверенного завершения миграции. Полный профиль CEF не шифруется оболочкой. |
| Planner checkpoints | Новые `shelter:planner:migration-backup:*`, `shelter:planner:preimport-backup:*`, `shelter:planner:unreadable-store-backup:*` сохраняются через `wpStoreEncrypt()` с read-back check | Старые plaintext backup keys могут остаться. При отказе шифрования/записи данные не должны переводиться в новую схему; задача остаётся read-only. `ss1` защищает новое значение, но не доказывает физическое удаление предыдущего значения из LevelDB. |
| Общие настройки/профиль | `shelter:lux` — внешний JSON в открытом виде: `prefs`, `ui`, `deep`, account/name/email, downloads, blocks; `chats`, `messages`, `passwords` вложены как отдельные защищённые blobs | Имена файлов, URL и локальные пути в обычных download-записях могут быть чувствительными. Содержимое `shelter:lux` целиком не шифруется. |
| AI-конфигурация | `shelter:ai` записывается через `ss1:` | Base URL/model/API key шифруются приложением на диске, но во время запроса ключ передаётся выбранному пользователем провайдеру. |
| Секретный master key | Windows: `secret.key` содержит DPAPI-protected bytes (`CryptProtectData`, текущий OS user). macOS: Keychain item `SHELTER/master-key` плюс raw master-key mirror `secret.key`, созданный с POSIX mode 0600 | На macOS права файла ограничивают другие локальные аккаунты, но не защищают mirror от офлайн-чтения диска/копии файла; Keychain ACL в исходнике допускает приложения без prompt. Нужна отдельная политика ключа/совместимости. Не проверено на реальных Windows/macOS системах. |
| CEF website profiles | `persist:space-<key>` задаёт per-space `cache_path` в user data dir и `persist_session_cookies=true`; `temp:ghost-<key>` создаётся без `cache_path` | Cookies, Local Storage, IndexedDB, cache и другие Chromium stores принадлежат CEF; полное шифрование этих профилей проектом не реализовано и состав файлов на диске runtime не проверялся. macOS при ошибке/таймауте Cookie Keychain включает `use-mock-keychain`; исходный комментарий указывает, что этот fallback не шифрует cookies. Windows/macOS cookie encryption не проверялась runtime в этой задаче. |
| Native metadata и журналы | В data dir также есть `cert_pins` (запомненные host + certificate fingerprint), `pending-wipe.txt` (очередь очистки profile IDs), `Extensions/<id>`/расширения и `shelter.log` с ротацией до 512 KiB + `.1`; macOS пишет `keychain.log` с результатами/OS-кодами Keychain | Исходник ограничивает/санитизирует многие поля журнала и на POSIX ставит `shelter.log` mode 0600; `keychain.log` тоже не должен содержать ключи или payload. ACL Windows, фактическая ретенция и runtime-содержимое файлов не проверялись. |
| Ghost downloads | UI не пишет ghost download metadata в обычный список; native comment явно допускает сохранение самого скачанного файла на диск | Ghost mode не гарантирует, что загруженный пользователем файл исчезнет. Закрытие/удаление CEF контекста не является secure erase. |

### Формат `ss1:` и legacy `p1:`

`src/secret_crypto.cc` показывает формат `ss1:`: base64-пакет с 12-byte nonce, ciphertext ChaCha20 и 32-byte HMAC-SHA256 tag; encryption/authentication keys выводятся от 32-byte master key разными HMAC-SHA256 labels. Мастер-ключ не передаётся renderer-коду; `host-bridge.js` вызывает native secret API через same-origin `shelter://app` endpoint. Это описание реализации, **не независимая криптографическая оценка**; алгоритм/формат не менялся.

UI распознаёт `p1:` как зашифрованный marker и сохраняет исходный блоб при ошибке. Однако native `DecryptWithKey()` в текущем C++ принимает только `ss1:`. Поэтому `p1:` сейчас нельзя честно описывать как читаемый legacy формат. Если у пользователей есть реальные `p1:` записи, до любого удаления/миграции нужен владелец-утверждённый план восстановления; текущая задача не меняет `ss1:` и не добавляет новый формат.

## Ghost mode: исправление и предел проверки

До правки `toggleGhost(true)` сохранял состояние, не вызывал `renderView()`, а `spacesForStore()` отфильтровывал только вкладки с `tab.ghost === true`. Уже открытая persistent-вкладка имела `ghost === false`; следовательно, её URL/stack/title оставались в JSON, а нативная вкладка могла продолжить работу в `persist:space-*` до следующего `view.open`. На выключении такая вкладка также могла не попасть в `closeGhostTabs()`.

В текущей правке:

1. при включении текущая вкладка помечается `ghost=true` до сохранения;
2. активный view сразу вызывает `renderView(false, 0)`, чтобы мост запросил `temp:ghost-*` вместо ожидания следующей навигации;
3. `window.__shelterTab` помечает вкладки, впервые открываемые при активном ghost mode;
4. выключение сначала закрывает помеченные ghost tabs, затем принудительно вызывает `renderView` для текущего persistent состояния;
5. `tests/ui_ghost_mode_test.js` проверяет filtering, смену active view и закрытие ghost tabs на уровне JS state.

Это source-level remediation, **не доказательство** того, что CEF полностью остановил in-flight requests, что старый контекст не успел записать cookie или что macOS/Windows storage ведёт себя одинаково. Нужен runtime acceptance-test на обеих ОС с синтетическим сайтом, set-cookie, URL/title/history, download, переключением режима и перезапуском. В Ghost всё ещё разрешены пользовательский network traffic и запись скачанного файла на диск.

## Сетевые соединения, обнаруженные в исходнике

Новые endpoint, telemetry и analytics в этой работе не добавлялись. Freshness warning вычисляется по embedded build timestamp и часам устройства; он не вызывает сеть/native bridge. P0 build/release workflow загружает CEF/minisign и создаёт GitHub attestation только в CI.

В текущем приложении исходники содержат следующие внешние запросы/потоки:

- **Обычные сайты и поисковики:** пользовательская навигация/поиск несёт обычный сетевой трафик и cookies выбранного профиля.
- **DNS-over-HTTPS:** default prefs — HTTPS-only включён, provider `cf`; при таком выборе DNS queries идут к Cloudflare (`cloudflare-dns.com`). В настройках доступны другие провайдеры или системный DNS. Резолвер видит IP и запрашиваемые имена согласно сетевой модели.
- **Проверка обновлений:** при включённом default preference запрос `GET https://api.github.com/repos/Everick-Ven/shelter/releases/latest` начинается примерно через минуту после старта и далее раз в сутки; пользователь также может проверить вручную. Код сравнивает локальную версию с публичным tag. В запросе нет пользовательского account/task payload, но GitHub видит сетевые метаданные соединения, например IP/время/HTTP headers.
- **Favicon:** CEF может запросить favicon у самого посещённого host; UI также просит native bridge догрузить иконки для bookmark/history. Код не использует сторонний favicon aggregation service; host, у которого запрошена иконка, получает соответствующий запрос. Ghost tab отсекается в native favicon path.
- **AI-агент:** только при пользовательской настройке/вызове. Native browser process отправляет prompt (в том числе выбранный пользователем текст задачи) и Bearer API key на HTTPS OpenAI-compatible base URL; HTTP разрешён только loopback для локального сервера.
- **Расширения:** загрузка CRX из Chrome Web Store — пользовательская операция. Установленное расширение имеет собственные сетевые возможности в рамках Chromium/оболочки; полный аудит расширений в этой фазе не проводился.

Статический поиск не обнаружил встроенного аналитического SDK/событийной отправки в сторонний telemetry endpoint. Это не заменяет packet-capture/runtime network audit. В рамках этого изменения существующие сетевые функции не расширялись и CSP/проверка моста не ослаблялись.

## Проверка release trust и CEF freshness

- `.github/workflows/build.yml` закрепляет third-party Actions полными commit SHA; release job разрешён только для `push` v* tag в официальном репозитории, а `workflow_dispatch` не запускает подпись. Для проверки подписи публичный ключ берётся checkout-ом `main` (не из самого release tag); checkout не сохраняет Git credentials.
- `SHA256SUMS.txt` создаётся по окончательным Setup/ZIP/DMG, подписывается detached minisign 0.12; minisign source tarball проверяется по SHA-256 `796dce1376f9bcb1a19ece729c075c47054364355fe0c0c1ebe5104d508c7db0`. Подпись проверяется repository public key до attestation и публикации. Signature не входит в собственный manifest.
- Provenance action `actions/attest-build-provenance@977bb373ede98d70efdf65b84cb5f73e068dcc2a` attests все финальные пакеты и checksum files. Это не заменяет independently trusted minisign fingerprint и не является CVE/security verdict.
- `.github/dependabot.yml` проверяет GitHub Actions еженедельно; `SECURITY.md` описывает private disclosure и ограничения поддержки/платформенных подписей.
- CEF SHA-256 pins из официальных `.sha256` sidecars и применяется к архиву до extraction/use. Сами архивы в этом аудите не скачивались и sidecar values независимо локально не пересчитывались; CMake configuration/build не запускались.
- Offline About warning появляется при возрасте build timestamp `>=90` дней. Он не проверяет наличие новой версии и не делает сетевой запрос. Runtime UI на Windows/macOS не проверялась.
- Gatekeeper/SmartScreen предупреждения не исправлены и не объявляются исправленными. Authenticode, Developer ID и notarization в этой фазе не добавлены.

### Невозможность подтвердить GitHub controls

По состоянию на запросы GitHub API в этой среде: `release-signing` environment lookup вернул 404; rulesets list вернул count 0; branch-protection endpoint вернул 403; `security_and_analysis` был `null`. Это не доказывает, что все настройки отсутствуют: часть API недоступна/непоказательна. Настройки не менялись. Владелец должен подтвердить их в GitHub Settings UI; workflow не может сам проверить наличие reviewers, tag rules или защиты 2FA.

## Результаты regression tests

- **39/39 JavaScript regression scripts** из `tests/*.js` прошли, включая `ui_tasks_test.js`, новый `ui_ghost_mode_test.js`, `ui_build_cache_test.js`, `app_build_freshness_test.js`, `ui_csp_test.js` и `security_hardening_source_test.js`.
- `python3 tools/check_workflow_security.py` прошёл после последних изменений workflow, включая trusted-main checkout и проверку secret scope.
- C++ compatibility test `tests/secret_crypto_test.cc` + `src/secret_crypto.cc` с `-Wall -Wextra -Werror -fno-exceptions` собран и вернул exit code 0; `tests/security_paths_test.cc` собран/запущен и вывел `SHELTER_SECURITY_PATHS_PASS`.
- `git diff --check` прошёл после исправления trailing whitespace в `docs/release-signing.md`.
- CMake/CEF configure/build (CMake отсутствует в этой среде), Windows/macOS runtime и release-signing workflow — не выполнялись.

## Владелец: решения и ручной чек-лист

### До любого реального подписанного релиза

1. Создать minisign 0.12 keypair офлайн по [`release-key.md`](release-key.md); приватный key/password не коммитить и не создавать в CI.
2. Заменить placeholder public key, независимо опубликовать fingerprint и проверить пару на тестовом файле.
3. В GitHub Settings создать/проверить `release-signing`, два Environment secrets, required reviewer, tag deployment rule `v*`, правила создания/перезаписи тегов и минимально необходимые publish permissions. Если reviewer/tag controls недоступны — сначала выбрать альтернативу; не считать YAML Environment достаточной защитой.
4. Только после этого выполнить отдельный контролируемый tag-release rehearsal и вручную проверить minisign, checksums и `gh attestation verify` для каждого пакета.

### До обещаний защиты данных/ghost privacy

1. Runtime на чистых Windows и macOS профилях: включить Ghost на авторизованной synthetic странице, проверить мгновенный переход на in-memory partition, отсутствие новых persistent cookies/URL/history/favcache/download metadata, а затем проверить закрытие вкладки при выключении.
2. Отдельно проверить, что сам скачанный файл в Ghost намеренно остаётся на диске, а ранее созданные persistent cookies/profile не считаются удалёнными.
3. Решить политику macOS master-key mirror: принять риск текущего same-user/offline-readable `secret.key` либо утвердить совместимую миграцию/изменение хранения. Не удалять mirror до проверки восстановления существующих `ss1:` данных и обновления.
4. На копии synthetic профиля перечислить legacy plaintext planner backups, Local Storage/LevelDB остатки и `p1:` blobs; прежде чем чистить или конвертировать, обеспечить проверенный экспорт/rollback. Не обещать secure erase на SSD/APFS/NTFS.
5. Сохранить поддержку чтения существующих данных; не менять wire format `ss1:` и не включать `ss2` без отдельного решения.

### Не запускать без отдельного согласования

P1: перепроектирование криптографии, изменение macOS key storage/hardened runtime и platform signing, отключение/изменение сетевых функций, аудит/изоляция расширений и fingerprint в UI. P2: libsodium-based in-app verification, Windows Authenticode, `ss2` migration, PIN/master-password. Эти этапы в текущей фазе не начинались.
