# Офлайн-ключ detached-подписи релизов

## Текущий блокер

`docs/release-pubkey.txt` пока содержит явный placeholder, а GitHub Environment `release-signing` и его approval/tag rules не были проверены. Job должен завершиться ошибкой на placeholder или отсутствующих secrets, но YAML declaration не доказывает и не проверяет reviewer approval/tag protection. До ручной проверки Environment и правил тегов владелец не должен добавлять secrets или создавать release tag. Не создавайте ключ в CI, не вставляйте приватный ключ в Git и не отключайте fail-closed проверку.

Подпись в workflow генерируется **minisign 0.12**, собранным из исходного архива с закреплённым SHA-256 `796dce1376f9bcb1a19ece729c075c47054364355fe0c0c1ebe5104d508c7db0`. Источник — официальный тег [jedisct1/minisign 0.12](https://github.com/jedisct1/minisign/tree/0.12). Репозиторный Linux-бинарник не используется: его digest здесь независимо не подтверждён.

## Создание ключа владельцем офлайн

1. На отдельной доверенной машине установите проверенную версию minisign 0.12. Подготовьте защищённый каталог и резервный носитель; не используйте рабочий каталог репозитория для секретного ключа.
2. Создайте зашифрованную пару ключей интерактивно. Не используйте `-W` (он отключает шифрование), не передавайте пароль аргументом команды и не включайте shell tracing:

   ```sh
   umask 077
   minisign -G -s shelter-release.key -p shelter-release-pubkey.txt
   ```

   Minisign запросит пароль дважды. Используйте уникальный сильный пароль из менеджера паролей. `shelter-release.key` остаётся только у владельца и на защищённой резервной копии.
3. Проверьте пару отдельным тестовым файлом: подпишите его интерактивно командой `minisign -S -s shelter-release.key -m test.txt`, затем выполните `minisign -V -p shelter-release-pubkey.txt -m test.txt`. После проверки удалите тестовый файл и подпись.
4. Опубликуйте только файл `shelter-release-pubkey.txt` как `docs/release-pubkey.txt`. Запишите fingerprint в README/релизную документацию и сообщите его пользователям по отдельному доверенному каналу. Не считайте fingerprint, впервые полученный с той же страницы релиза, независимой точкой доверия.
5. Используемый в проекте fingerprint — SHA-256 от base64-декодированной второй строки minisign public-key файла (целой структуры с алгоритмом, key number и открытым ключом). Он воспроизводится стандартной библиотекой Python:

   ```sh
   python3 -c 'import base64,hashlib,pathlib; rows=pathlib.Path("docs/release-pubkey.txt").read_bytes().splitlines(); print(hashlib.sha256(base64.b64decode(rows[1], validate=True)).hexdigest())'
   ```

   Сравните результат с отдельно опубликованным 64-символьным hex fingerprint. Публичный ключ должен успешно проверять тестовую подпись до добавления в репозиторий.

## Настройка закрытых GitHub secrets

В **Settings → Environments** создайте Environment с точным именем `release-signing`.

- Добавьте `RELEASE_MINISIGN_SECRET_KEY_BASE64` (base64 полного **текстового файла** `shelter-release.key`) и `RELEASE_MINISIGN_PASSWORD` (его пароль) как Environment secrets. Не создавайте их как repository-wide secrets и не добавляйте в workflow logs/artifacts.
- Перед вводом убедитесь, что приватный key file зашифрован minisign, его тестовая подпись проверяется опубликованным ключом, а резервная копия существует. Ограничьте доступ к Environment только владельцами релиза.
- Настройте required reviewer approval и deployment tag rule `v*`, если эти controls доступны для текущего плана GitHub. Дополнительно защитите создание/обновление `v*` tags через repository ruleset и ограничьте право публикации релизов.
- Если требуемое reviewer/tag protection недоступно, **не добавляйте secrets и не выпускайте подписанные релизы** до выбора владельцем альтернативного контроля.

Workflow декодирует временный ключ в файл с правами, ограниченными `umask 077`, подписывает только финальный `assets/SHA256SUMS.txt`, проверяет подпись опубликованным public key, вычисляет fingerprint и удаляет временный файл через `trap`. Пароль передаётся через stdin minisign; он не кладётся в командную строку или artifact. Не меняйте этот порядок и не делайте signing secrets доступными build/PR jobs.

## Смена или компрометация ключа

Ротация ключа — отдельное решение владельца. Сгенерируйте новую пару офлайн, опубликуйте новый ключ и fingerprint с объяснением даты перехода, безопасно обновите Environment secrets и настройте порядок проверки старых выпусков. Не удаляйте старый public key/fingerprint из истории проверки. При возможной компрометации немедленно остановите релизный workflow и отзовите/удалите Environment secrets; не пытайтесь скрыть или переподписать уже опубликованные артефакты без явного уведомления.
