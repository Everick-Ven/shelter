'use strict';

// Консолидированный source-тест харднинга (аудит 2026-10-19): нативные
// правки, которые нельзя скомпилировать без CEF, фиксируются поиском
// устойчивых якорей в исходниках. H2 (шифрование secret.key) осознанно
// отложен — нотаризации macOS нет, тест его не требует.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const has = (text, needle, label) =>
  assert.ok(text.includes(needle), `нет якоря: ${label}`);
const no = (text, needle, label) =>
  assert.ok(!text.includes(needle), `запрещённый якоря: ${label}`);

const shellCc = read('src/shell.cc');
const shellH = read('src/shell.h');
const bridge = read('src/shell_bridge.cc');
const clientsCc = read('src/clients.cc');
const clientsH = read('src/clients.h');
const renderer = read('src/renderer_app.cc');
const netguardCc = read('src/netguard.cc');
const netguardH = read('src/netguard.h');
const scheme = read('src/ui_scheme.cc');
const commonCc = read('src/common.cc');
const pslH = read('src/public_suffix.h');
const netUtilsH = read('src/net_utils.h');
const dlNameH = read('src/download_name.h');
const avcH = read('src/app_version_compare.h');
const hostBridge = read('resources/ui/host-bridge.js');
const indexHtml = read('resources/ui/index.html');

// M1: общий PSL вместо ручного списка — делегирование в common.cc.
has(commonCc, '#include "src/public_suffix.h"', 'M1 include');
has(pslH, 'kobe.jp', 'M1 правила со звёздочками в PSL');

// M2: ClearPrivacy чистит открытые вкладки через журнал начал разделов.
has(shellH, 'RecordPartitionOrigin', 'M2 декларация');
has(shellH, 'partition_origins_', 'M2 журнал');
has(shellCc, 'partition_origins_[t->partition]', 'M2 запись в журнале');
has(bridge, 'partition_origins_.erase(p)', 'M2 журнал стирается после чистки');

// M3: MV2 document_start больше не теряется — очередь в рендерер.
has(renderer, 'g_pending_ext_scripts', 'M3 очередь скриптов');
has(renderer, 'std::map<std::string, PendingExtScripts> g_pending_ext_scripts',
    'M3 очередь ключевана строковым id фрейма (CEF 154: GetIdentifier -> CefString)');
has(renderer, 'shelter.extscripts', 'M3 канал доставки');
has(shellCc, 'PushExtScriptsToFrame', 'M3 отправка из браузера свободной функцией');
no(shellCc, 'Shell::PushExtScripts', 'M3 нет метода внутри анонимного неймспейса (C2888)');
has(shellCc, 'SetString(1, url)', 'M3 браузер прикладывает URL подбора скриптов');
has(renderer, 'frame->GetV8Context()', 'M3 немедленное выполнение при готовом контексте');
has(renderer, 'pending.url', 'M3 отложенная запись привязана к URL');
has(renderer, 'SHELTER_EXTSCRIPTS', 'M3 диагностический лог порядка событий');

// M4: сторонние куки и песочница приватности закрыты по умолчанию.
has(netguardH, 'ApplyPrivacyPrefs', 'M4 декларация');
has(netguardCc, 'ApplyPrivacyPrefs', 'M4 реализация');
has(netguardCc, 'block_third_party_cookies', 'M4 тумблер 3P');

// M5: опасные загрузки предупреждают до сохранения.
has(shellCc, 'DownloadDanger', 'M5 классификация');
has(shellCc, '\\"danger\\":', 'M5 поле в событии dlprompt');
has(indexHtml, 'Скачать всё равно', 'M5 кнопка осознанного риска');
has(indexHtml, 'dlAskNext', 'M5 диалог решения');

// M6: проверка обновлений через официальные релизы GitHub.
has(shellCc, 'api.github.com/repos/Everick-Ven/shelter/releases/latest',
    'M6 официальный URL');
has(shellCc, 'OnUpdateCheckDone', 'M6 итог проверки');
has(shellCc, 'update-status', 'M6 событие в UI');
has(avcH, 'CompareAppVersions', 'M6 числовой компаратор');
has(bridge, 'app.checkUpdates', 'M6 метод моста');
has(hostBridge, "q('app.checkUpdates'", 'M6 метод host-bridge');
has(indexHtml, 'requestUpdateCheck(true)', 'M6 ручная проверка');
has(indexHtml, "case 'update-status'", 'M6 диспетчер события');
has(indexHtml, 'scheduleUpdateChecks', 'M6 автопроверка раз в сутки');
no(indexHtml, "toast('У вас последняя версия — ' + VERSION, { icon: 'check' }); }, 1300)",
   'M6 фейковая кнопка без запроса удалена');

// M7: приватные адреса не проваливаются в fail-open.
has(netguardCc, '#include "src/net_utils.h"', 'M7 общие утилиты');
has(netguardCc, 'LowerHost(', 'M7 единый lower хоста');
has(netUtilsH, 'IsPrivateHost', 'M7 предикат приватности');

// L1: подсказка UI экранирует путь.
has(scheme, 'HtmlEscape(looked_in)', 'L1 эскейп @DIR@');

// L2: имена загрузок санитизируются до UniquePath.
has(dlNameH, 'SanitizeDownloadLeaf', 'L2 санитайзер');
has(shellCc, 'SanitizeDownloadLeaf(name)', 'L2 вызов в UniquePath');

// L3: фингерпринт-маркеры маскируют toString.
has(renderer, 'function mask(f,o)', 'L3 маска toString');
has(renderer, 'mask(CanvasRenderingContext2D', 'L3 маска canvas');

// L4: разрешения выдаются осознанно, а не deny-all.
has(shellH, 'AllowSitePermissions', 'L4 декларация выдачи');
has(shellCc, 'SitePermissionsAllowed', 'L4 проверка выдачи');
has(clientsCc, 'CEF_PERMISSION_RESULT_ACCEPT', 'L4 accept в клиенте');
has(bridge, 'perm.allow', 'L4 метод моста');
has(hostBridge, 'permAllow', 'L4 метод host-bridge');
has(indexHtml, 'permAllow', 'L4 кнопка «Разрешить»');

// L5: ошибки сертификатов решает пользователь, а не deny-all.
has(clientsH, 'OnCertificateError', 'L5 декларация в клиенте');
has(clientsCc, 'TabClient::OnCertificateError', 'L5 перехват в клиенте');
has(shellCc, 'OnCertError', 'L5 очередь решений');
has(shellCc, 'cert_pins', 'L5 файл запомненных пинов');
has(shellCc, 'cert-error', 'L5 событие в UI');
has(bridge, 'cert.decision', 'L5 метод моста');
has(hostBridge, 'certDecision', 'L5 метод host-bridge');
has(indexHtml, "case 'cert-error'", 'L5 диспетчер события');
has(indexHtml, 'certAsk', 'L5 диалог решения');

// L6 осознанно не трогаем: мёртвые p1:-ветки держат JS-тесты.

// L7: синхронный мост секретов ограничен восемью мегабайтами.
has(scheme, 'kMaxSecretRequestBytes = 8u * 1024u * 1024u', 'L7 кап 8 МБ');

console.log('security_hardening_source_test: OK');
