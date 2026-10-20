'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { webcrypto } = require('crypto');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');
const marker = '/* =========================================================\n   SHELTER · Планировщик задач (v7 · дедлайн завершает диапазон)';
const start = html.indexOf(marker);
assert(start >= 0, 'the v7 planner module must be installed in the active UI');
const end = html.indexOf('\ninit();\n', start);
assert(end > start, 'the planner module must load before the application initializes');
const planner = html.slice(start, end);
const domSelectStart = html.indexOf('function wpNormalizeDomSelectOptions(options) {');
const domSelectEnd = html.indexOf("\ndocument.addEventListener('pointerdown', e => {\n  const sel = e.target.closest && e.target.closest('select.select');", domSelectStart);
assert(domSelectStart >= 0 && domSelectEnd > domSelectStart, 'the reusable DOM-list controls must be present before native-select compatibility hooks');
const domSelectSource = html.slice(domSelectStart, domSelectEnd);
assert.strictEqual((planner.match(/function wpAllView\(/g) || []).length, 1, 'the task-list renderer has one definition and preserves the indexed filter/count path');
assert(planner.includes('<select') && /type=["']date["']/.test(planner), 'planner task screens match the reference Tasks layout (native filter and date controls 1:1)');
assert(html.includes('function openSelectMenu(sel) {') && html.includes('wpRestoreSelectFocus'), 'native task selects open through the custom menu overlay');
assert.strictEqual((html.match(/closest\('select\.select'\)/g) || []).length, 2, 'exactly one pointerdown/mousedown pair intercepts native selects (a duplicate would toggle the menu shut)');
assert(planner.includes('function wpOpenNativeDateCalendar(input)') && planner.includes("String(date).slice(0,7)"), 'native task date inputs route through the built-in calendar instead of the broken CEF system picker');
const legacyTaskStart = html.indexOf('function tasksBody() {');
const legacyTaskEnd = html.indexOf('\nfunction taskMenu(anchor)', legacyTaskStart);
assert(legacyTaskStart >= 0 && legacyTaskEnd > legacyTaskStart, 'the legacy task view and editor source must be present');
const legacyTaskSource = html.slice(legacyTaskStart, legacyTaskEnd);
assert(!legacyTaskSource.includes('<select') && !/type=["']date["']/.test(legacyTaskSource), 'the legacy task view and editor must not contain native select or date-picker controls');

assert(html.includes('<style id="shelter-week-planner-style">'), 'the task planner stylesheet must be present');
const plannerStyle = html.match(/<style id="shelter-week-planner-style">([\s\S]*?)<\/style>/);
assert(plannerStyle && plannerStyle[1].includes('.wp-filter-layout{display:grid;'), 'the list and board filters share a responsive grid');
assert(plannerStyle[1].includes('.wp-all-filter-row .wp-filter-sort') && plannerStyle[1].includes('grid-row:5'), 'the task-list sort control stays readable at narrow widths');
assert(plannerStyle[1].includes('@container shelter (max-width:620px)'), 'the shared filter grid adapts to the app viewport');
assert(plannerStyle[1].includes('.wp-date-jump input{position:absolute'), 'the calendar-jump input stays invisible under its icon button');
const weekGridCss = html.match(/\.wp-week-grid\.is-all-day\{([^}]*)\}/);
assert(weekGridCss && /display:grid/.test(weekGridCss[1]) && /grid-template-columns:repeat\(7,minmax\(0,1fr\)\)/.test(weekGridCss[1]) && /gap:10px/.test(weekGridCss[1]), 'week day columns render as history-style cards in a seven-column grid');
assert(!html.includes('.wp-week-grid.is-all-day .wp-day{'), 'week day columns reuse the base card frame instead of a flat override');
assert(!html.includes('.wp-week-grid.is-all-day .wp-week-days>.wp-day+.wp-day'), 'week day cards are separated by grid gaps, not divider lines');
assert(html.includes('.wp-week-grid.is-all-day .wp-week-days{display:contents}'), 'the week-days wrapper flattens so the cards become grid items');
assert(html.includes('wpMigrateTaskStores(breg)'), 'legacy task migration must run during load');
assert(html.includes("blobToStore('__boardBlob', S.board)") && html.includes("blobToStore('__boardsBlob'"), 'periodic task saves encrypt boards through the secret store');
assert(html.includes('wpStoreParse(rawBoardStore)') && html.includes('wpStoreParse(rawBoardsStore)'), 'task loading decrypts before parsing');
assert(!html.includes("store.put('shelter:board'") && !html.includes("store.put('shelter:boards'"), 'boards are never written as plaintext JSON');
assert(planner.includes('function wpFinalizeTaskMigration()'), 'migration must require an explicit confirmation step');
assert(planner.includes('WP._searchTimer=setTimeout') && planner.includes("search.addEventListener('input',schedule)") && planner.includes("search.addEventListener('compositionend',schedule)"), 'task search must be debounced and keep its clear control in sync');
assert(planner.includes("if(page && page.querySelector('#wpView') && !page.querySelector('.wp-migration-error')) { wpRenderView(); return; }"), 'task-page updates patch the existing planner view instead of replacing the page');
assert(planner.includes('wpCaptureTaskEntities(editRef,{includeRules:!') && planner.includes("(v.frequency!=='none'||recurring)") && !planner.includes('const before=wpClone(S.board), newId=isNew?'), 'task-editor saves use a focused rollback snapshot and copy recurrence rules only when needed');
const taskPersistStart=planner.indexOf('function wpPersistBoard()');
const taskPersistEnd=planner.indexOf('\nfunction wpUndoSnapshot',taskPersistStart);
const taskPersistSource=planner.slice(taskPersistStart,taskPersistEnd);
assert(taskPersistSource.includes("store.set('shelter:board',encRaw)") && taskPersistSource.includes("store.get('shelter:boards')!==encRegRaw") && taskPersistSource.includes('wpStoreEncrypt(raw)') && taskPersistSource.includes('wpStoreEncrypt(regRaw)') && !taskPersistSource.includes('JSON.parse') && !taskPersistSource.includes('saveNow()'), 'task updates encrypt and verify exact board writes without parsing a second copy or rewriting unrelated application data');
assert(!taskPersistSource.includes('scheduleAchievementReconcile'), 'task-board persistence must not trigger achievement reconciliation');
const statusUpdateStart=planner.indexOf('function wpSetStatusRef(ref,key) {');
const statusUpdateEnd=planner.indexOf('\nfunction wpApplySchedule',statusUpdateStart);
const statusUpdateSource=planner.slice(statusUpdateStart,statusUpdateEnd);
assert(statusUpdateStart>=0&&statusUpdateEnd>statusUpdateStart&&statusUpdateSource.includes('wpCommit(`Статус изменён'), 'status changes must flow through the planner commit path');
assert(!/refresh\s*:\s*false/.test(statusUpdateSource), 'status changes keep a full view refresh because status affects filters, columns, and derived counts');
const saveNowStart = html.indexOf('function saveNow() {');
const saveNowEnd = html.indexOf("\ndocument.addEventListener('visibilitychange'", saveNowStart);
assert(saveNowStart >= 0 && saveNowEnd > saveNowStart && html.slice(saveNowStart, saveNowEnd).includes('scheduleAchievementReconcile()'), 'profile changes continue to reconcile achievements through the general save path');
const profileActivityStart = html.indexOf('function profileActivity(raw = false) {');
const profileActivityEnd = html.indexOf('\nfunction storedProfileUnlocks()', profileActivityStart);
assert(profileActivityStart >= 0 && profileActivityEnd > profileActivityStart, 'the active profile-activity calculation must be present');
const profileActivitySource = html.slice(profileActivityStart, profileActivityEnd);
assert(!/\bS\.(?:board|boards)\b/.test(profileActivitySource), 'achievement metrics must not read task boards');
const runProfileActivity = profileState => {
  const transient = { premiumServerEntitlement: { verified: true, firstPaymentConfirmed: true }, premiumVerificationStatus: 'checked' };
  const engines = { google: {}, bing: {}, yandex: {} };
  const hostOf = value => { try { return new URL(value).hostname.toLowerCase(); } catch (_) { return ''; } };
  const profileDomainMatch = (host, roots) => roots.some(root => host === root || host.endsWith('.' + root));
  const profileTabKind = value => {
    const host = hostOf(value);
    if (profileDomainMatch(host, ['music.yandex.ru', 'spotify.com'])) return 'music';
    if (profileDomainMatch(host, ['youtube.com', 'youtu.be'])) return 'video';
    if (profileDomainMatch(host, ['wikipedia.org', 'habr.com'])) return 'reading';
    return '';
  };
  const profileKnownSearchEngines = () => [...new Set((profileState.ui.profileSearchEngines || []).filter(key => engines[key]))];
  const activity = new Function('S', 'T', 'PROFILE_PRIVACY_PROTECTIONS', 'ENGINES', 'profileKnownSearchEngines', 'profileCounter', 'hostOf', 'urlMeta', 'visitDur', 'profileDomainMatch', 'profileTabKind', `${profileActivitySource}\nreturn profileActivity;`)(
    profileState, transient,
    [{ key: 'trackers' }, { key: 'https' }, { key: 'fp' }, { key: 'strict' }], engines,
    profileKnownSearchEngines, () => 0, hostOf,
    value => ({ kind: hostOf(value) === 'search.example' ? 'search' : 'site' }),
    visit => Math.max(0, Number(visit.duration) || 0), profileDomainMatch, profileTabKind);
  return activity();
};
const profileAt = Date.UTC(2026, 1, 1, 12);
const profileFixture = {
  visits: [
    { url: 'https://gosuslugi.ru/', at: profileAt, duration: 30 },
    { url: 'https://stepik.org/lesson', at: profileAt + 60000, duration: 600 },
    { url: 'https://youtube.com/watch', at: profileAt + 120000, duration: 90 },
    { url: 'https://search.example/?q=one', at: profileAt + 150000, duration: 0 }
  ],
  spaces: { main: { tabs: [{ url: 'https://music.yandex.ru/' }, { url: 'https://wikipedia.org/' }] } },
  prefs: { trackers: true, https: true, fp: false, strict: true },
  bookmarks: [{ id: 'b1' }, { id: 'custom-1' }],
  ui: { profileSearchEngines: ['google', 'bing'] },
  ghost: false
};
const profileBeforeTasks = JSON.stringify(runProfileActivity(profileFixture));
profileFixture.board = { tasks: [{ id: 'private-task', title: 'private task text', statusKey: 'done' }] };
profileFixture.boards = [{ tasks: profileFixture.board.tasks }];
assert.strictEqual(JSON.stringify(runProfileActivity(profileFixture)), profileBeforeTasks, 'adding or changing tasks cannot alter profile achievements because profileActivity derives only from browser activity and preferences');
const pwStoreStart = html.indexOf('function pwToStore(list) {');
const pwStoreEnd = html.indexOf('\nfunction aiFromStore', pwStoreStart);
assert(pwStoreStart >= 0 && pwStoreEnd > pwStoreStart, 'password encryption storage functions must be available for regression tests');
const pwStoreSource = html.slice(pwStoreStart, pwStoreEnd);
const makePasswordStore = (state, encrypt, decrypt) => new Function('S', 'secretEnc', 'secretDec', `${pwStoreSource}\nreturn { pwToStore, pwFromStore };`)(state, encrypt, decrypt);
let passwordEncryptCount = 0;
const encryptedPasswords = new Map();
const passwordState = {};
const passwordStore = makePasswordStore(passwordState, serialized => {
  const blob = `p1:test:${++passwordEncryptCount}`;
  encryptedPasswords.set(blob, serialized);
  return blob;
}, blob => encryptedPasswords.get(blob) || '');
const passwordFixture = [{ id: 'saved-1', site: 'example.invalid', username: 'user', password: 'initial-secret' }];
const firstPasswordBlob = passwordStore.pwToStore(passwordFixture);
assert.strictEqual(passwordStore.pwToStore(passwordFixture), firstPasswordBlob, 'unchanged passwords reuse the memoized ciphertext');
assert.strictEqual(passwordEncryptCount, 1, 'unchanged saves do not repeat synchronous password encryption');
passwordFixture[0].password = 'updated-secret';
const updatedPasswordBlob = passwordStore.pwToStore(passwordFixture);
assert.notStrictEqual(updatedPasswordBlob, firstPasswordBlob, 'changing a password produces an updated stored ciphertext');
assert.strictEqual(passwordEncryptCount, 2, 'a changed password is encrypted once and then cached');
assert.deepStrictEqual(JSON.parse(encryptedPasswords.get(updatedPasswordBlob)), passwordFixture, 'the updated ciphertext represents the changed password list');
assert.strictEqual(passwordStore.pwToStore(passwordFixture), updatedPasswordBlob, 'subsequent saves reuse the new ciphertext');
assert.strictEqual(passwordEncryptCount, 2);
const lockedState = {};
let lockedEncryptCount = 0;
const lockedPasswordStore = makePasswordStore(lockedState, () => { lockedEncryptCount++; return 'unexpected'; }, () => '');
const unreadablePasswordBlob = 'p1:unreadable-stored-data';
assert.deepStrictEqual(lockedPasswordStore.pwFromStore(unreadablePasswordBlob), [], 'a blob that cannot be decrypted remains unavailable');
assert.strictEqual(lockedPasswordStore.pwToStore([{ password: 'must-not-overwrite' }]), unreadablePasswordBlob, 'an unreadable existing blob remains preserved instead of being overwritten');
assert.strictEqual(lockedEncryptCount, 0, 'the protected unreadable-blob path does not invoke encryption');
assert(planner.includes('restored.focus({preventScroll:true})') && planner.includes('setSelectionRange(focusState.start,focusState.end)'), 'planner updates restore focused controls and text selection');
assert(planner.includes('function wpOpenFilterMenu(anchor)') && planner.includes('if(wpTaskMenuOwned())closeMenu();'), 'filter dropdowns use the shared popup menu and close it before rerendering the view');
assert(planner.includes("const search=page&&page.querySelector('#wpSearch'); if(search&&search.value!==WP.query) search.value=WP.query;"), 'view changes preserve the planner search query');
assert(planner.includes("kb('⌘⇧K')"), 'the task search shortcut label must adapt to the platform');
assert(planner.includes("await wpAddProjectFromEditor(target,projectSelection)") && planner.includes('wpPersistBoard()'), 'the task editor persists a project selected from its add-project option');
assert(planner.includes("kind==='deadline'?'Крайний срок':'Дата начала'") && !planner.includes('Плановая дата'), 'the planner consistently calls the scheduled start date “Дата начала”');
assert(planner.includes('plannerCalendarShowCompletedTasks') && planner.includes('plannerBoardShowCompletedTasks'), 'calendar and board completed-visibility preferences persist independently');
assert(!planner.includes('WP.showCompleted'), 'completed-visibility state is not shared across planner views');
assert(planner.includes('function wpExportTasks() { wpOpenTaskExportDialog(); }'), 'task export opens the protected scope and password dialog');
assert(planner.includes("if(!wpIsTaskExportEnvelope(envelope))"), 'task import rejects JSON files without the encrypted envelope');
assert(planner.includes('const payload=await wpDecryptTaskPayload(envelope,password)'), 'task import authenticates and decrypts before passing payload to the importer');
assert(planner.includes('function wpMergeImportedTaskData(') && planner.includes('wpMergeImportedTaskData(wpTaskTransferBoards(),sourceBoards'), 'a fully decrypted export is merged with local boards before it is persisted');
assert(planner.includes('Совпадающие идентификаторы сохранят локальные записи') && planner.includes('новую доску из файла приложение выберет автоматически'), 'the import confirmation explains non-destructive merge and automatic selection of a new imported board');
const settingsExportLine = html.split(/\r?\n/).find(line => line.includes("else if (a === 'export')"));
assert(settingsExportLine && settingsExportLine.includes('shelterExportBackup()'), 'the general settings export opens the encrypted backup dialog');
const backupPayloadLine = html.split(/\r?\n/).find(line => line.includes("kind: 'SHELTER_BROWSER_BACKUP'"));
assert(backupPayloadLine && backupPayloadLine.includes('spaces: S.spaces') && backupPayloadLine.includes('prefs: S.prefs') && !backupPayloadLine.includes('S.board'), 'the browser backup payload carries spaces, bookmarks, visits and prefs without task boards');
assert(html.includes('await wpEncryptTaskPayload(backupPayload, code)'), 'the browser backup is encrypted with the file code before download');
assert(html.includes("'.shelter'"), 'the encrypted browser backup uses the .shelter extension');
assert(!html.includes("'shelter-backup-' + dayKey(Date.now()) + '.json'"), 'the browser backup is never exported as plaintext JSON');
assert(html.includes("else if (a === 'import') shelterImportBackup();"), 'settings offer an encrypted backup restore');
assert(html.includes("payload.kind !== 'SHELTER_BROWSER_BACKUP'"), 'backup restore rejects decrypted files without the browser-backup marker');
const backupImportFn = html.slice(html.indexOf('function shelterImportBackup()'), html.indexOf('function shelterApplyBrowserBackup('));
assert(backupImportFn.includes("input.type = 'file'") && !backupImportFn.includes('input.accept'), 'the backup picker stays unfiltered so .shelter files remain selectable');
assert(html.includes('shelterIsLegacyBrowserBackup(parsed)'), 'old plaintext backups stay readable through the legacy fallback');
assert(html.includes('function wpDeadlineBeforeStart(scheduledDate,deadlineAt)'), 'the deadline invariant helper is installed');
assert(html.includes('Крайний срок не может быть раньше даты начала.'), 'setting a deadline before the start date is rejected');
assert(html.includes('Дата начала не может быть позже крайнего срока.'), 'moving the start date past the deadline is rejected');
assert(!html.includes("downloadFile('shelter-tasks.json'"), 'the legacy task export path does not generate plaintext JSON');
assert(html.includes("'shelter-tasks-backup-'+wpDateKey(Date.now())+'.shelter'"), 'task exports share the .shelter extension with browser backups');
assert(!html.includes("downloadFile('shelter-planner-migration-backup.shelter',raw)"), 'the technical migration snapshot is password-protected too');
const taskImportFn = html.slice(html.indexOf('function wpImportFile()'), html.indexOf('function wpReviewSelect('));
assert(taskImportFn.includes("input.type='file'") && !taskImportFn.includes('input.accept'), 'the task picker stays unfiltered so .shelter files remain selectable');
const downloadFileSource = html.slice(html.indexOf('function downloadFile('), html.indexOf('\n/* =========================================================\n   ДАШБОРД', html.indexOf('function downloadFile(')));
assert(downloadFileSource.includes('link.click()') && !downloadFileSource.includes('Сохранено:'), 'UI exports trigger a real download without claiming success before the file is saved');
const clientsHeader = fs.readFileSync(path.join(__dirname, '..', 'src', 'clients.h'), 'utf8');
const clientsSource = fs.readFileSync(path.join(__dirname, '..', 'src', 'clients.cc'), 'utf8');
const uiClientStart = clientsHeader.indexOf('class UiClient');
const uiClientEnd = clientsHeader.indexOf('// Клиент вкладки', uiClientStart);
const uiClientHeader = clientsHeader.slice(uiClientStart, uiClientEnd);
assert(uiClientHeader.includes('public CefDownloadHandler') && uiClientHeader.includes('GetDownloadHandler()'), 'the chrome-UI CEF client handles generated file downloads');
const uiDownloadStart = clientsSource.indexOf('bool UiClient::OnBeforeDownload');
const uiDownloadEnd = clientsSource.indexOf('void UiClient::OnDownloadUpdated', uiDownloadStart);
const uiDownloadHandler = clientsSource.slice(uiDownloadStart, uiDownloadEnd);
assert(uiDownloadHandler.includes('Shell::Get().IsUiBrowser(browser)') && uiDownloadHandler.includes('IsUiUrl(frame->GetURL().ToString())') && uiDownloadHandler.includes('callback->Continue(default_path, true)'), 'the UI-origin-only native handler opens a Save As dialog for generated files');
assert(uiDownloadHandler.includes('path_utils::PathFromUtf8(downloads)') && uiDownloadHandler.includes('path_utils::PathToUtf8('), 'Save As converts filesystem UTF-8 bytes through the shared C++17/C++20-safe path helper');

const harness = String.raw`
const DAY = 86400000;
const __data = globalThis.__data;
const __state = globalThis.__state;
let __uid = 0;
const store = {
  get(k) { return __data.has(k) ? __data.get(k) : null; },
  set(k, v) {
    if (globalThis.__denyBackup && String(k).startsWith('shelter:planner:migration-backup')) return;
    if (globalThis.__failNextStoreKey === k) { globalThis.__failNextStoreKey = ''; return; }
    __data.set(k, String(v));
  },
  del(k) { __data.delete(k); },
  json(k, d) { const v = this.get(k); if (v == null || v === '') return d; try { return JSON.parse(v); } catch (_) { return d; } },
  put(k, v) { this.set(k, JSON.stringify(v)); }
};
const secretEnc = s => globalThis.__failSecret ? '' : 'ss1:' + String(s);
const secretDec = s => String(s).startsWith('ss1:') ? String(s).slice(4) : '';
const S = __state;
const RENDER = {}, AFTER = {}, ACT = {}, SEG = {};
const OV = { stack: [] };
const document = { activeElement: null, addEventListener() {} };
const window = globalThis.__window || {};
const REDUCED = () => globalThis.__reducedMotion === true;
const $ = () => null, $$ = () => [];
const pad2 = n => String(n).padStart(2, '0');
const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
const uid = prefix => String(prefix || 'id') + '-test-' + (++__uid);
const boardCode = () => 'SHTR-TEST';
const parseRu = s => { const m = String(s || '').match(/^(\d{1,2})\.(\d{1,2})\.(\d{4})$/); return m ? new Date(+m[3], +m[2] - 1, +m[1]) : null; };
const accountEmail = () => String((S.account && S.account.email) || '').trim().toLowerCase();
const accountName = () => (S.account && S.account.name) || (S.ui && S.ui.profileName) || 'Вы';
const defaultBoard = () => ({ id: '', name: 'Моя доска', code: '', projects: [], people: ['Вы'], prios: [], terms: [], formats: [], statuses: [], tasks: [], invites: [], ownerEmail: '' });
let __ensureBoardCount=0;
const ensureBoard = () => { __ensureBoardCount++; if (!S.board) S.board = defaultBoard(); if (!Array.isArray(S.board.tasks)) S.board.tasks = []; if (!Array.isArray(S.board.projects)) S.board.projects = []; };
const canSeeTask = () => true;
const isBoardOwner = () => true;
const isPersonalTask = t => !t || t.kind === 'personal' || (!t.kind && !t.project);
const visibleProjects = () => (S.board && S.board.projects) || [];
const addProject = name => { const p = { id: uid('prj'), name, members: [] }; S.board.projects.push(p); return p; };
const plural = (n, forms) => { const a = Math.abs(n) % 100, b = a % 10; return a > 10 && a < 20 ? forms[2] : b > 1 && b < 5 ? forms[1] : b === 1 ? forms[0] : forms[2]; };
const fmtDur = n => String(n);
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const ico = () => '';
const toast = () => {};
const downloadFile = () => {};
const openMenu = () => {};
const openModal = options => { globalThis.__lastModalHtml = options && options.html || ''; return { scrim: { addEventListener() {}, querySelector() { return null; } }, guard: null }; };
const closeModal = () => {};
const confirmDlg = async () => true;
const promptDlg = async () => 'test';
const taskDicts = () => {};
const projectInvite = () => {};
let __saveNowCount=0;
const saveNow = () => { __saveNowCount++; };
const persist = () => {};
const refreshPage = () => {};
const renderBadges = () => {};
const curTab = () => null;
const urlMeta = () => ({ page: 'tasks' });
const storeUnused = null;
const kb = s => s;
`;

const state = {
  board: null, boards: null, boardsMeta: {}, boardId: '', ui: {}, account: null,
  taskMigrationOK: true, taskMigrationPending: false, taskMigrationError: '',
  taskMigrationPendingKeys: [], taskMigrationBackupKey: '', taskMigrationOriginalRaw: null,
  taskMigrationPreviewShown: false
};
const data = new Map();
const context = { __state: state, __data: data, __denyBackup: false, __failNextStoreKey: '', __failSecret: false, __window: { crypto: webcrypto, addEventListener() {}, removeEventListener() {} }, __reducedMotion: false, crypto: webcrypto, TextEncoder, TextDecoder, btoa, atob, console, setTimeout, clearTimeout, innerWidth: 1280, innerHeight: 800 };
vm.runInNewContext(harness + '\n' + domSelectSource + '\n' + planner + `
globalThis.__api = {
  schema: WP_SCHEMA, WP, S, store, saveNowCount:()=>__saveNowCount, ensureBoardCount:()=>__ensureBoardCount, actions: ACT, render: RENDER, after: AFTER,
  wpDateKey, wpDeadlineBeforeStart, wpWeekStart, wpAddDays, wpMonthStart, wpMonthKeys, wpFmtMonth, wpMatchRuleDate,
  wpTaskHomeType, wpIsMine, wpAssigneeLabel, wpAssigneeChoices, wpCanonicalAssignee, wpRenameSelfAssignee, WP_SELF_ASSIGNEE, wpCurrentAssignee, wpCanCommentTask, wpTaskCommentList, wpAddTaskComment, wpCommentThreadHtml, wpCalendarEntities, wpWeekEntities, wpMonthEntities, wpRangeEntities, wpCreateTask, wpQuickSubmit, wpContextAddTarget, wpApplyTaskForm,
  wpTaskScheduleStart, wpTaskScheduleEnd, wpTaskScheduleOffset, wpTaskCoversDate, wpTaskHasDateSpan, wpEffectiveOccurrence, wpApplySchedule, wpCalendarShownEntities, wpToggleCalendarCompleted,
  wpDeadlineText, wpDeadlineOverdue, wpTaskCard, wpDayColumn, wpDayCapsule, wpCalendarSpanLayout, wpWeekCalendar, wpMonthDots, wpMonthDay, wpMonthWeek, wpMonthSummary, wpMonthMini, wpMonthMiniDay, wpMonthMiniToggle, wpToggleMonthMini, wpSelectedDayPanel, wpSetupTaskTitleMarquee, wpDateFieldHtml, wpEditorProjectOptions, wpEditorBaseValues, wpEditorBodyHtml, wpHeaderControls, wpNavHtml, wpViewHtml, wpValidPriorityColor, wpBoardPriorityColors, wpPriorityVarsStyle, wpPriorityVarsAttr, wpDefaultPriorityColors, wpApplyEntityHome,
  wpEnsureState, wpSetCalendarMode, wpCalendarMove, wpToggleSidebar, wpCalendarCompletedToggle, wpSelectCalendarDate,
  wpBoardEntities, wpBoardSummary, wpBoardToolbar, wpBoardView, wpBoardColumns, wpBoardLaneHeader, wpTaskStatus, wpStatusLabel, wpStatusKeys, wpStatusOptions, wpDefaultStatusKey, wpValidStatusKey, wpNormalizeStatusesArray, wpNormalizeStatusFlags, wpStatusKeyForName, wpStatusKeyForNewName, wpMakeBoardStatus, wpMoveBoardStatus, wpStatusFlags, wpStatusDone, wpStatusOutdated, wpStatusQuiet, wpStatusClosed, wpTaskDone, wpTaskOutdated, wpTaskQuiet, wpTaskClosed, wpSetBoardFilter, wpSetBoardFocus, wpToggleBoardMine, wpResetBoardFilters, wpAllEntities, wpSortEntities, wpAllTagChoices, wpBaseTasks, wpSetAllViewFilter, wpResetAllFilters, wpAllFilterToolbar, wpCaptureTaskEntities, wpRestoreTaskEntities, wpPersistBoard, wpCommit, wpRenderTaskPageFull, wpRenderView,
  wpNormalizeBoard, wpStoreEncrypt, wpStoreDecrypt, wpStoreParse, wpMigrateTaskStores, wpFinalizeTaskMigration,
  wpSetStatusRef, wpTaskTransferBoards, wpTaskProjectId, wpTaskProjectChoices, wpExportBoardProjects, wpExportPayload, wpSelfTaskId, wpTaskContacts, wpSaveTaskContact, wpTaskContactOptionsHtml, wpApplyTransferRemap, wpPersonName, wpPersonContactId, wpMakePerson, wpBoardPersonIndex, wpBoardAddPerson, wpBoardPersonIndexById, wpDeleteTaskContact, wpContactIdUsedAnywhere,
  wpEncryptTaskPayload, wpDecryptTaskPayload, wpReadTaskExportBoards, wpMergeImportedProjects, wpMergeImportedTaskData, wpApplyImportedTaskData,
  wpTaskPayloadFromMigrationBackupFile, wpDownloadMigrationBackup
};`, context, { timeout: 5000 });
const api = context.__api;

// Date arithmetic stays local-date based and rejects impossible calendar dates.
assert.strictEqual(api.wpDateKey('13.10.2026'), '2026-10-13');
assert.strictEqual(api.wpDateKey('2026-02-30'), '');
assert.strictEqual(api.wpWeekStart('2026-10-13'), '2026-10-12');
assert.strictEqual(api.wpAddDays('2026-12-31', 1), '2027-01-01');
assert.strictEqual(api.wpMonthStart('2026-10-13'), '2026-10-01');
assert.strictEqual(api.wpFmtMonth('2026-10-13'), 'Октябрь 2026');
assert.strictEqual(api.wpDeadlineBeforeStart('2026-10-10', '2026-10-09'), true);
assert.strictEqual(api.wpDeadlineBeforeStart('2026-10-10', '2026-10-10'), false);
assert.strictEqual(api.wpDeadlineBeforeStart('2026-10-10', '2026-10-11'), false);
assert.strictEqual(api.wpDeadlineBeforeStart(null, '2026-10-09'), false);
assert.strictEqual(api.wpDeadlineBeforeStart('2026-10-10', ''), false);
assert.strictEqual(api.wpDeadlineBeforeStart('10.10.2026', '09.10.2026'), true);
const octoberGrid = Array.from(api.wpMonthKeys('2026-10-01'));
assert.strictEqual(octoberGrid.length, 35);
assert.strictEqual(octoberGrid[0], '2026-09-28');
assert.strictEqual(octoberGrid.at(-1), '2026-11-01');
const augustGrid = Array.from(api.wpMonthKeys('2026-08-01'));
assert.strictEqual(augustGrid.length, 35, 'a 42-cell month is cropped to a strict 7x5 grid');
assert.strictEqual(augustGrid[0], '2026-08-03');
assert.strictEqual(augustGrid.at(-1), '2026-09-06');
const februaryGrid = Array.from(api.wpMonthKeys('2021-02-01'));
assert.strictEqual(februaryGrid.length, 35, 'a short month is padded to a strict 7x5 grid');
assert.strictEqual(februaryGrid[0], '2021-02-01');
assert.strictEqual(februaryGrid.at(-1), '2021-03-07');

// Calendar views include only the signed-in person's tasks, while accepting the Russian self-assignee aliases.
state.account = { name: 'Alex', email: 'alex@example.test' };
state.board = {
  id: 'calendar-board', ownerEmail: 'alex@example.test', projects: [{ id: 'project-1', name: 'Проект', members: [] }, { id: 'project-2', name: 'Другой проект', members: [] }],
  people: [], invites: [], occurrences: [],
  recurrenceRules: [
    { id: 'weekly-self', taskId: 'recurring-self', frequency: 'weekly', interval: 1, weekdays: [1], startDate: '2026-10-05', endDate: null, deadlineOffsetDays: 1, active: true },
    { id: 'weekly-other', taskId: 'recurring-other', frequency: 'weekly', interval: 1, weekdays: [1], startDate: '2026-10-05', endDate: null, active: true }
  ],
  tasks: [
    { id: 'self-you', title: 'Исполнитель You', homeType: 'personal', assignee: 'YOU', scheduledDate: '2026-10-12', statusKey: 'open', priorityKey: 'normal', order: 1 },
    { id: 'self-ya', title: 'Исполнитель Я', homeType: 'inbox', assignee: 'я', scheduledDate: '2026-10-13', statusKey: 'open', priorityKey: 'normal', order: 2 },
    { id: 'self-name', title: 'Имя учётной записи', homeType: 'project', projectId: 'project-1', assignee: 'Alex', scheduledDate: '2026-10-14', statusKey: 'open', priorityKey: 'normal', order: 3 },
    { id: 'self-email', title: 'Почта учётной записи', homeType: 'personal', assignee: 'ALEX@example.test', scheduledDate: '2026-10-15', statusKey: 'open', priorityKey: 'normal', order: 4 },
    { id: 'self-legacy-matrix', title: 'Старая задача Матрицы', homeType: 'matrix', assignee: 'Вы', scheduledDate: '2026-10-16', statusKey: 'open', priorityKey: 'normal', qm: 1, order: 5 },
    { id: 'self-next-week', title: 'Следующая неделя', homeType: 'personal', assignee: 'Я', scheduledDate: '2026-10-20', statusKey: 'open', priorityKey: 'normal', order: 6 },
    { id: 'other-person', title: 'Чужая задача', homeType: 'personal', assignee: 'Другой исполнитель', scheduledDate: '2026-10-13', statusKey: 'open', priorityKey: 'normal', order: 7 },
    { id: 'unassigned', title: 'Без исполнителя', homeType: 'personal', assignee: '', scheduledDate: '2026-10-13', statusKey: 'open', priorityKey: 'normal', order: 8 },
    { id: 'recurring-self', title: 'Повтор личной задачи', homeType: 'personal', assignee: 'я', scheduledDate: '2026-10-05', deadlineAt: '2026-10-06', recurrenceRuleId: 'weekly-self', statusKey: 'open', priorityKey: 'normal', order: 9 },
    { id: 'recurring-other', title: 'Повтор чужой задачи', homeType: 'personal', assignee: 'Другой исполнитель', scheduledDate: '2026-10-05', recurrenceRuleId: 'weekly-other', statusKey: 'open', priorityKey: 'normal', order: 10 },
    { id: 'project-assigned-you', title: 'Проектная задача Вы', homeType: 'project', projectId: 'project-1', project: 'Проект', assignee: 'Вы', scheduledDate: '2026-10-13', statusKey: 'open', priorityKey: 'normal', order: 11 },
    { id: 'project-assigned-ya', title: 'Задача второго проекта Я', homeType: 'project', projectId: 'project-2', project: 'Другой проект', assignee: 'Я', scheduledDate: '2026-10-14', statusKey: 'open', priorityKey: 'normal', order: 12 },
    { id: 'project-other-assignee', title: 'Чужая задача второго проекта', homeType: 'project', projectId: 'project-2', project: 'Другой проект', assignee: 'Другой исполнитель', scheduledDate: '2026-10-14', statusKey: 'open', priorityKey: 'normal', order: 13 }
  ]
};
api.WP.query = '';
api.WP.mobileDate = '2026-10-13'; api.WP.weekStart = '2026-10-12'; api.WP.monthStart = '2026-10-01';
api.WP.calendarMode = 'week'; api.WP.view = 'week'; api.WP._calendarModeRestored = true;
assert.strictEqual(api.wpIsMine({ assignee: 'ВЫ' }), true);
assert.strictEqual(api.wpIsMine({ assignee: 'Я' }), true);
assert.strictEqual(api.wpIsMine({ assignee: 'Alex' }), true);
assert.strictEqual(api.wpIsMine({ assignee: 'alex@example.test' }), true);
assert.strictEqual(api.wpIsMine({ assignee: 'Другой исполнитель' }), false);
assert.strictEqual(api.wpAssigneeLabel({ assignee: 'Я' }), 'Alex');
assert.strictEqual(api.wpAssigneeLabel({ assignee: 'ВЫ' }), 'Alex');
assert.strictEqual(api.wpAssigneeLabel({ assignee: 'ALEX@example.test' }), 'Alex');
assert.strictEqual(api.wpAssigneeLabel({ assignee: 'me' }), 'Alex', 'the stored self marker resolves to the profile name');
assert.strictEqual(api.wpCanonicalAssignee('Alex'), 'me');
assert.strictEqual(api.wpCanonicalAssignee('я'), 'me');
assert.strictEqual(api.wpCanonicalAssignee('ALEX@example.test'), 'me');
assert.strictEqual(api.wpCanonicalAssignee(''), '', 'unassigned tasks stay unassigned');
assert.strictEqual(api.wpCanonicalAssignee('Другой исполнитель'), 'Другой исполнитель', 'foreign assignees keep their literal names');
assert.strictEqual(api.WP_SELF_ASSIGNEE, 'me');
const savedAccount = state.account;
state.account = null; state.ui.profileName = 'Гость';
assert.strictEqual(api.wpCurrentAssignee(), 'Гость', 'the profile name identifies the browser user without login');
state.account = savedAccount; delete state.ui.profileName;
const assigneeChoices = Array.from(api.wpAssigneeChoices());
assert.strictEqual(assigneeChoices.filter(name => name === 'Alex').length, 1, 'self aliases collapse into one account-name option');
for (const alias of ['Вы', 'Я', 'вы', 'я', 'YOU', 'me', 'alex@example.test']) assert(!assigneeChoices.includes(alias), `assignee choices must not expose the self alias ${alias}`);
const selfAliasCard = api.wpTaskCard(state.board.tasks.find(task => task.id === 'self-ya'), { compact: true });
assert(selfAliasCard.includes('Исполнитель: Alex') && selfAliasCard.includes('<span>Alex</span>'), "compact cards display the signed-in user's name instead of Вы/Я");
const legacyAliasBoard = api.wpNormalizeBoard({ id: 'alias-board', name: 'Доска', people: ['Вы', 'Анна'], tasks: [{ id: 'alias-task', title: 'Старый исполнитель', assignee: 'Вы' }, { id: 'other-task', title: 'Чужой исполнитель', assignee: 'Анна' }] });
assert.strictEqual(legacyAliasBoard.tasks.find(task => task.id === 'alias-task').assignee, 'me', 'stored Вы aliases migrate to the self marker on load');
assert.strictEqual(api.wpAssigneeLabel(legacyAliasBoard.tasks.find(task => task.id === 'alias-task')), 'Alex');
assert.strictEqual(legacyAliasBoard.tasks.find(task => task.id === 'other-task').assignee, 'Анна');
assert(legacyAliasBoard.people.includes('Alex') && !legacyAliasBoard.people.includes('Вы'), 'board people migrate stored self aliases to the account name');
const boardBeforeRename = state.board;
const staleBoard = { id: 'stale-board', tasks: [{ id: 'stale-self', title: 'Старое имя', assignee: 'Alex' }, { id: 'stale-other', title: 'Чужое имя', assignee: 'Анна' }], people: ['Alex', 'Анна'] };
state.board = staleBoard; state.boards = [staleBoard];
state.account.name = 'Борис';
assert.strictEqual(api.wpRenameSelfAssignee('Alex', 'Борис'), 1, 'renaming the profile remaps tasks with the previous name');
assert.strictEqual(staleBoard.tasks[0].assignee, 'me');
assert.strictEqual(api.wpAssigneeLabel(staleBoard.tasks[0]), 'Борис', 'self-assigned tasks follow the profile name');
assert.strictEqual(staleBoard.tasks[1].assignee, 'Анна', 'foreign assignees survive a profile rename');
assert.deepStrictEqual(JSON.parse(JSON.stringify(staleBoard.people)), ['Борис', 'Анна']);
state.account.name = 'Alex'; state.board = boardBeforeRename; state.boards = null;
// Both calendar modes span every visible project on the active board, even if a project detail was open previously.
api.WP.projectId = 'project-1';
const weekIds = new Set(Array.from(api.wpWeekEntities('2026-10-12'), task => task.id));
assert.deepStrictEqual([...weekIds].sort(), ['project-assigned-ya', 'project-assigned-you', 'recurring-self::2026-10-12', 'self-email', 'self-legacy-matrix', 'self-name', 'self-ya', 'self-you'].sort());
assert(!weekIds.has('project-other-assignee'), 'week view must exclude project tasks assigned to another person');
const monthIds = new Set(Array.from(api.wpMonthEntities('2026-10-01'), task => task.id));
assert(monthIds.has('project-assigned-you') && monthIds.has('project-assigned-ya'), 'month view must include the signed-in user’s tasks from multiple projects');
assert(!monthIds.has('project-other-assignee'), 'month view must exclude project tasks assigned to another person');
api.WP.projectId = '';
assert(monthIds.has('self-next-week'), 'month view must include all of the current month, beyond the selected week');
assert(monthIds.has('recurring-self::2026-10-26'), 'month view must materialize weekly recurring tasks');
assert(!monthIds.has('other-person') && !monthIds.has('recurring-other::2026-10-12'), 'neither one-off nor recurring tasks for other assignees belong in the personal calendar');
assert(!monthIds.has('unassigned'), 'the calendar must not treat an unassigned task as the signed-in user’s task');
const recurrenceOverlapIds = new Set(Array.from(api.wpRangeEntities('2026-10-06', '2026-10-12'), task => task.id));
assert(recurrenceOverlapIds.has('recurring-self::2026-10-05'), 'a repeating multi-day task remains visible when its start is before the requested range');
const recurringSpan = state.board.tasks.find(task => task.id === 'recurring-self');
assert.strictEqual(api.wpEffectiveOccurrence(recurringSpan, '2026-10-19', null).deadlineAt, '2026-10-20', 'recurrence instances derive their deadline from the series offset');
assert.strictEqual(api.wpEffectiveOccurrence(recurringSpan, '2026-10-19', { id: 'recurring-self::2026-10-19', scheduledDate: '2026-10-19', deadlineAt: null }).deadlineAt, null, 'an occurrence can explicitly clear its inherited deadline');
api.WP.calendarMode = 'week';
const weekMarkup = api.wpViewHtml();
assert(weekMarkup.includes('Проектная задача Вы') && weekMarkup.includes('Задача второго проекта Я') && !weekMarkup.includes('Чужая задача второго проекта'), 'week markup must show the signed-in user’s assigned tasks across projects only');
api.WP.calendarMode = 'month';
const monthMarkup = api.wpViewHtml();
assert(monthMarkup.includes('wp-month-grid') && monthMarkup.includes('Октябрь 2026'));
assert(monthMarkup.includes('Исполнитель Я') && monthMarkup.includes('Проектная задача Вы'), 'the selected-day panel lists that date’s tasks with titles');
assert(monthMarkup.includes('wp-month-dots'), 'month cells render history-style priority dots');
const oct14Cell = api.wpMonthDay('2026-10-14', Array.from(api.wpMonthEntities('2026-10-01')));
assert(oct14Cell.includes('<span class="wp-month-count">2</span>') && (oct14Cell.match(/<i class="p-/g) || []).length === 2, 'month cells dot only the signed-in user’s tasks for that date');
assert(!monthMarkup.includes('Чужая задача'));
assert(monthMarkup.includes('data-act="wpMonthMiniToggle"') && monthMarkup.includes('Компактный календарь'), 'the month summary offers the mini-calendar toggle');
const miniDay = api.wpMonthMiniDay('2026-10-14', Array.from(api.wpMonthEntities('2026-10-01')));
assert(miniDay.startsWith('<button type="button" class="wp-month-mini-day') && miniDay.includes('data-date="2026-10-14"') && miniDay.includes('<span class="wp-month-mini-num">14</span>'), 'mini days are compact date buttons');
assert(miniDay.includes('data-drop-date="2026-10-14"'), 'mini days accept dragged tasks like full cells');
assert.strictEqual((miniDay.match(/<i class="p-/g) || []).length, 2, 'mini days dot their tasks by priority');
const miniEmpty = api.wpMonthMiniDay('2026-10-21', Array.from(api.wpMonthEntities('2026-10-01')));
assert(miniEmpty.includes('<span class="wp-month-mini-dots"></span>'), 'empty mini days keep the dot row for uniform rows');
api.WP.monthMini = true;
const miniMarkup = api.wpViewHtml();
assert(miniMarkup.includes('wp-month-layout is-mini') && miniMarkup.includes('wp-month-mini-grid') && !miniMarkup.includes('class="wp-month-grid"') && miniMarkup.includes('wp-month-selected'), 'mini mode collapses the month into the side miniature with the day panel on the right');
const miniSelectedBtn = miniMarkup.match(/<button[^>]*data-date="2026-10-13"[^>]*>/)[0];
assert(miniSelectedBtn.includes('is-selected'), 'the miniature marks the selected day');
state.board.tasks.push({ id: 'mini-note', title: 'С заметкой', homeType: 'personal', assignee: 'me', scheduledDate: '2026-10-13', statusKey: 'open', priorityKey: 'normal', description: 'Подробности дня', tags: [] });
assert(api.wpViewHtml().includes('Подробности дня'), 'mini mode shows detailed day tasks with notes');
api.WP.monthMini = false;
assert(!api.wpViewHtml().includes('Подробности дня'), 'the full month keeps compact day cards');
state.board.tasks.pop();
api.wpToggleMonthMini();
assert.strictEqual(api.WP.monthMini, true);
assert.strictEqual(state.ui.plannerMonthMini, true, 'the mini toggle persists');
api.wpToggleMonthMini();
assert.strictEqual(api.WP.monthMini, false);
assert.strictEqual(state.ui.plannerMonthMini, false);
const calendarModeBeforeMiniRestore = api.WP.calendarMode;
api.WP._monthMiniRestored = false; state.ui.plannerMonthMini = true; api.wpEnsureState();
assert.strictEqual(api.WP.monthMini, true, 'the mini mode restores from saved state');
api.WP._monthMiniRestored = true; api.WP.monthMini = false; state.ui.plannerMonthMini = false; api.WP.calendarMode = calendarModeBeforeMiniRestore;
assert(!api.wpNavHtml().includes('data-view="matrix"') && !api.wpNavHtml().includes('label:"Матрица"'), 'Matrix must no longer be a planner navigation destination');
const rangeTasks = [
  { id: 'span-before-month', title: 'Срок на границе месяца', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-09-25', deadlineAt: '2026-10-02', statusKey: 'open', priorityKey: 'normal', matrixQuadrant: 1, order: 20, tags: [] },
  { id: 'week-span', title: 'Задача на несколько дней', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-10', deadlineAt: '2026-10-13', statusKey: 'open', priorityKey: 'high', matrixQuadrant: 0, order: 21, tags: [] },
  { id: 'month-span-a', title: 'Подготовка A', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-14', deadlineAt: '2026-10-16', statusKey: 'open', priorityKey: 'critical', matrixQuadrant: 1, order: 22, tags: [] },
  { id: 'month-span-b', title: 'Подготовка B', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-14', deadlineAt: '2026-10-17', statusKey: 'open', priorityKey: 'none', matrixQuadrant: 2, order: 23, tags: [] },
  { id: 'single-day-task', title: 'Однодневная задача', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-15', deadlineAt: null, statusKey: 'open', priorityKey: 'normal', matrixQuadrant: null, order: 24, tags: [] }
];
state.board.tasks.push(...rangeTasks);
assert(api.wpTaskCoversDate(rangeTasks[0], '2026-10-01'));
assert(!api.wpTaskCoversDate(rangeTasks[0], '2026-10-03'));
assert(api.wpTaskHasDateSpan(rangeTasks[0]));
const monthRangeIds = new Set(Array.from(api.wpMonthEntities('2026-10-01'), task => task.id));
assert(monthRangeIds.has('span-before-month'), 'month view must include ranges that begin before its visible grid');
api.WP.monthStart = '2026-10-01'; api.WP.mobileDate = '2026-10-01'; api.WP.calendarMode = 'month';
const rangeMonthMarkup = api.wpViewHtml();
assert(rangeMonthMarkup.includes('wp-month-layout') && rangeMonthMarkup.includes('wp-month-calendar') && rangeMonthMarkup.includes('wp-month-selected'), 'the month grid and selected-day panel share the new responsive layout wrapper');
assert(!rangeMonthMarkup.includes('wp-month-span-layer') && !rangeMonthMarkup.includes('wp-calendar-span--month'), 'month cells show dots only, without horizontal span rows');
assert(!rangeMonthMarkup.includes('wp-month-span-spacer'), 'month cells keep no spacer for the removed span layer');
assert(rangeMonthMarkup.includes('data-act="wpCalendarToggleCompleted"') && rangeMonthMarkup.includes('Скрыть выполненные задачи'), 'month calendar offers the completed-task visibility toggle');
const rangeMonthGridOnly = rangeMonthMarkup.slice(rangeMonthMarkup.indexOf('<div class="wp-month-grid"'), rangeMonthMarkup.indexOf('wp-month-selected'));
assert(!rangeMonthGridOnly.includes('Задача на несколько дней') && !rangeMonthGridOnly.includes('data-id="week-span"'), 'multi-day titles leave the month grid (dots only)');
assert(rangeMonthGridOnly.includes('wp-month-dots'), 'covered month cells keep priority dots');
assert(rangeMonthMarkup.includes('Задача на несколько дней'), 'the selected-date panel keeps full cards for covered ranges');
assert(!rangeMonthMarkup.includes('Срок на границе месяца') && !rangeMonthMarkup.includes('25 сент. 2026 — 2 окт. 2026'), 'ranges outside the selected date leave no titles or span rows');
assert(rangeMonthMarkup.includes('data-act="wpQuickDay"'), 'month dates retain their own add-task controls');
const weekDays = Array.from({ length: 7 }, (_, i) => api.wpAddDays('2026-10-12', i));
const countId = (markup, id, attribute='data-id') => (markup.match(new RegExp(`${attribute}="${id}"`, 'g')) || []).length;
const monthWeekMarkup = api.wpMonthWeek(octoberGrid.slice(14, 21), rangeTasks);
assert.strictEqual(countId(monthWeekMarkup, 'week-span'), 0, 'multi-day tasks leave no rows in the month week');
assert.strictEqual(countId(monthWeekMarkup, 'month-span-a'), 0, 'overlapping ranges become dots instead of rows');
assert.strictEqual(countId(monthWeekMarkup, 'month-span-b'), 0, 'the end date is dotted like any other covered day');
assert(monthWeekMarkup.includes('<i class="p-critical"></i>') && monthWeekMarkup.includes('<i class="p-none"></i>'), 'month dots cover multi-day tasks by priority');
const singleDayCell = api.wpMonthDay('2026-10-15', rangeTasks);
assert(singleDayCell.includes('<span class="wp-month-dots"><i class="p-critical"></i><i class="p-none"></i><i class="p-normal"></i></span>'), 'one-day and multi-day tasks share the priority dots of their date cell');
assert(singleDayCell.includes('<span class="wp-month-count">3</span>'), 'the month count covers every task on the date');
const weekLayout = api.wpCalendarSpanLayout(weekDays, rangeTasks);
assert.strictEqual(weekLayout.laneCount, 2, 'overlapping all-day ranges occupy separate horizontal lanes');
assert.strictEqual(weekLayout.segments.length, 3, 'only tasks with a date range become horizontal spans');
assert(weekLayout.segments.find(segment => segment.task.id === 'week-span').continuesBefore, 'week range rows clip cleanly at the week boundary');
const crowdedSpans = [0, 1, 2].map(i => ({ id: `crowd-${i}`, title: `Перекрытие ${i}`, homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-13', deadlineAt: '2026-10-15', statusKey: 'open', priorityKey: 'normal', order: 30 + i, tags: [] }));
const crowdedLayout = api.wpCalendarSpanLayout(weekDays, crowdedSpans, 2);
assert.strictEqual(crowdedLayout.laneCount, 2, 'month weeks cap horizontal lanes so tiles stay compact');
assert.strictEqual(crowdedLayout.segments.filter(segment => segment.lane < 0).length, 1, 'ranges beyond the lane cap are marked as overflow');
const crowdedMarkup = api.wpMonthWeek(weekDays, crowdedSpans);
assert.strictEqual(countId(crowdedMarkup, 'crowd-0'), 0);
assert.strictEqual(countId(crowdedMarkup, 'crowd-1'), 0);
assert.strictEqual(countId(crowdedMarkup, 'crowd-2'), 0, 'overlapping ranges render no rows in the month week');
assert(crowdedMarkup.includes('<span class="wp-month-dots"><i class="p-normal"></i><i class="p-normal"></i><i class="p-normal"></i></span>'), 'overlapping ranges share the day dots instead of vanishing');
const rangeWeekCards = api.wpWeekCalendar(weekDays, rangeTasks);
assert(rangeWeekCards.includes('wp-calendar-span-layer wp-week-span-layer'), 'week mode restores its horizontal multi-day layer');
assert.strictEqual(countId(rangeWeekCards, 'week-span'), 1, 'the week view renders each spanning task once, not as a card in each covered day');
assert.strictEqual(countId(rangeWeekCards, 'month-span-a'), 1);
assert.strictEqual(countId(rangeWeekCards, 'month-span-b'), 1);
assert.strictEqual(countId(rangeWeekCards, 'single-day-task', 'data-task-id'), 1, 'single-day tasks remain capsules in their own week cell');
assert(rangeWeekCards.includes('wp-day-capsule p-normal') && rangeWeekCards.includes('Однодневная задача'), 'week day cells render thin capsules like the multi-day rows');
assert(!rangeWeekCards.includes('<article class="wp-task'), 'week day cells no longer duplicate full task cards');
assert(rangeWeekCards.includes('wp-calendar-span wp-calendar-span--week p-high'), 'week span color follows urgency');
assert(rangeWeekCards.includes('Приоритет: Критичный') && rangeWeekCards.includes('Приоритет: Без приоритета'), 'week range labels expose the priority');
assert(rangeWeekCards.includes('wp-day-span-spacer'), 'week days reserve room for the horizontal range lanes');
const desktopDay = api.wpDayColumn('2026-10-15', rangeTasks);
assert(desktopDay.includes('data-act="wpCalendarSelectDate" data-date="2026-10-15"'), 'clicking the whole week day selects it, not just its date label');
assert.strictEqual(countId(desktopDay, 'month-span-a'), 0, 'desktop date cells do not duplicate a range that is shown in the span layer');
assert.strictEqual(countId(desktopDay, 'single-day-task', 'data-task-id'), 1);
assert(desktopDay.includes('wp-day-capsule') && !desktopDay.includes('<article class="wp-task'), 'desktop date cells render capsules instead of cards');
const desktopDayHead = desktopDay.slice(desktopDay.indexOf('<header class="wp-day-head">'), desktopDay.indexOf('</header>'));
assert(/<div><b>\d+<\/b><span>/.test(desktopDayHead) && !desktopDayHead.includes('<small>'), 'week day headers mirror history: number first, then the weekday, no month label');
const monthDayCell = api.wpMonthDay('2026-10-15', rangeTasks);
assert(monthDayCell.includes('data-act="wpCalendarSelectDate" data-date="2026-10-15"'), 'clicking anywhere in a month day cell selects its date');
const selectedMobileDay = api.wpDayColumn('2026-10-15', rangeTasks, { includeMultiDay: true });
assert.strictEqual(countId(selectedMobileDay, 'month-span-a', 'data-task-id'), 1, 'the selected/mobile day list may keep capsules for multi-day tasks');
state.ui = { plannerShowCompletedTasks: true };
api.WP._completedVisibilityRestored = false;
api.wpEnsureState();
assert.strictEqual(api.WP.calendarShowCompleted, true, 'completed calendar tasks remain visible by default');
assert.strictEqual(api.WP.boardShowCompleted, true, 'completed board tasks remain visible by default');
const completedCalendarTasks = [
  { id: 'done-span', title: 'Завершённый диапазон', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-14', deadlineAt: '2026-10-16', statusKey: 'done', priorityKey: 'high', matrixQuadrant: 0, order: 25, tags: [] },
  { id: 'done-single', title: 'Завершённый день', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-15', deadlineAt: null, statusKey: 'done', priorityKey: 'normal', matrixQuadrant: 2, order: 26, tags: [] }
];
const completedWeekMarkup = api.wpWeekCalendar(weekDays, completedCalendarTasks);
assert(completedWeekMarkup.includes('wp-calendar-span wp-calendar-span--week p-high is-done'), 'completed multi-day tasks keep their horizontal rows');
assert.strictEqual(countId(completedWeekMarkup, 'done-span'), 1);
assert.strictEqual(countId(completedWeekMarkup, 'done-single', 'data-task-id'), 1, 'completed single-day tasks remain in their date cell');
assert(completedWeekMarkup.includes('<details class="wp-done-fold" open>'), 'visible completed single-day tasks are expanded in the week calendar');
const completedMonthMarkup = api.wpMonthWeek(octoberGrid.slice(14, 21), completedCalendarTasks);
assert(!completedMonthMarkup.includes('wp-calendar-span--month'), 'completed ranges leave no month rows either');
assert(completedMonthMarkup.includes('<span class="wp-month-dots"><i class="p-high"></i><i class="p-normal"></i></span>'), 'completed tasks keep priority dots in their date cell');
assert(completedWeekMarkup.includes('Статус: выполнена'), 'calendar controls announce completed status to assistive technology');
assert(api.wpCalendarCompletedToggle('calendar').includes('data-scope="calendar"') && api.wpCalendarCompletedToggle('calendar').includes('aria-pressed="true"'));
assert(api.wpCalendarCompletedToggle('board').includes('data-scope="board"') && api.wpCalendarCompletedToggle('board').includes('aria-pressed="true"'));
api.WP.calendarShowCompleted = false;
const hiddenCompletedWeek = api.wpWeekCalendar(weekDays, completedCalendarTasks);
const hiddenCompletedMonth = api.wpMonthWeek(octoberGrid.slice(14, 21), completedCalendarTasks);
const hiddenCompletedMobile = api.wpDayColumn('2026-10-15', completedCalendarTasks, { includeMultiDay: true });
assert(!hiddenCompletedWeek.includes('done-span') && !hiddenCompletedWeek.includes('done-single'), 'the calendar toggle hides completed week rows and capsules');
assert(!hiddenCompletedMonth.includes('done-span') && !hiddenCompletedMonth.includes('done-single'), 'the calendar toggle hides completed month dots and events');
assert(!hiddenCompletedMobile.includes('done-span') && !hiddenCompletedMobile.includes('done-single'), 'the calendar toggle also applies to the selected mobile day');
assert(api.wpCalendarCompletedToggle('calendar').includes('Показать выполненные задачи') && api.wpCalendarCompletedToggle('calendar').includes('aria-pressed="false"'));
assert(api.wpCalendarCompletedToggle('board').includes('Скрыть выполненные задачи') && api.wpCalendarCompletedToggle('board').includes('aria-pressed="true"'), 'hiding calendar completions does not change the Board button');
api.actions.wpCalendarToggleCompleted({ dataset: { scope: 'calendar' } });
assert.strictEqual(api.WP.calendarShowCompleted, true, 'the calendar action reveals completed tasks again');
assert.strictEqual(state.ui.plannerCalendarShowCompletedTasks, true, 'the calendar visibility preference is persisted separately');
assert.strictEqual(api.WP.boardShowCompleted, true, 'toggling calendar visibility leaves the board unchanged');
api.actions.wpCalendarToggleCompleted({ dataset: { scope: 'calendar' } });
assert.strictEqual(api.WP.calendarShowCompleted, false);
api.actions.wpCalendarToggleCompleted({ dataset: { scope: 'board' } });
assert.strictEqual(api.WP.boardShowCompleted, false, 'the board has its own completed-task toggle');
assert.strictEqual(api.WP.calendarShowCompleted, false, 'toggling the board leaves calendar visibility unchanged');
assert.strictEqual(state.ui.plannerBoardShowCompletedTasks, false, 'the board visibility preference is persisted separately');
api.actions.wpCalendarToggleCompleted({ dataset: { scope: 'board' } });
assert.strictEqual(api.WP.boardShowCompleted, true);
assert.strictEqual(api.WP.calendarShowCompleted, false);
api.actions.wpCalendarToggleCompleted({ dataset: { scope: 'calendar' } });
assert.strictEqual(api.WP.calendarShowCompleted, true);

// A legacy single visibility preference seeds both independent view preferences once.
state.ui = { plannerShowCompletedTasks: false };
api.WP._completedVisibilityRestored = false;
api.wpEnsureState();
assert.strictEqual(api.WP.calendarShowCompleted, false);
assert.strictEqual(api.WP.boardShowCompleted, false);
state.ui = { plannerShowCompletedTasks: true, plannerCalendarShowCompletedTasks: false, plannerBoardShowCompletedTasks: true };
api.WP._completedVisibilityRestored = false;
api.wpEnsureState();
assert.strictEqual(api.WP.calendarShowCompleted, false, 'a new calendar preference takes precedence over the legacy shared value');
assert.strictEqual(api.WP.boardShowCompleted, true, 'a new board preference takes precedence independently');
api.WP.calendarShowCompleted = true;
api.WP.boardShowCompleted = true;

// Board completion visibility is independent from the calendar, and cancelled cards are not treated as completed.
const priorBoardTasks=state.board.tasks, priorOccurrences=state.board.occurrences;
state.board.tasks=[
  { id:'board-open',title:'В работе',homeType:'personal',assignee:'Alex',statusKey:'open',priorityKey:'normal',order:1 },
  { id:'board-done',title:'Выполнена',homeType:'personal',assignee:'Alex',statusKey:'done',priorityKey:'normal',order:2 },
  { id:'board-cancelled',title:'Отменена',homeType:'personal',assignee:'Alex',statusKey:'cancelled',priorityKey:'normal',order:3 },
  { id:'board-urgent',title:'Срочная',homeType:'personal',assignee:'Alex',statusKey:'open',priorityKey:'high',order:4 },
  { id:'board-overdue',title:'Просроченная',homeType:'personal',assignee:'Alex',statusKey:'open',priorityKey:'normal',deadlineAt:'2000-01-01',order:5 },
  { id:'board-other',title:'Чужая срочная',homeType:'personal',assignee:'Morgan',statusKey:'open',priorityKey:'critical',order:6 }
];
state.board.occurrences=[]; api.WP.query=''; api.WP.boardHomeFilter='all'; api.WP.boardAssigneeFilter='all'; api.WP.boardMineOnly=false; api.WP.boardFocus='all';
api.WP.allFilter='cancelled'; api.WP.calendarShowCompleted=true; api.WP.boardShowCompleted=true;
let boardIds=api.wpBoardEntities().map(task=>task.id);
assert(boardIds.includes('board-done') && boardIds.includes('board-cancelled'), 'the board shows completed and cancelled tasks when its own toggle is on');
api.WP.boardShowCompleted=false;
boardIds=api.wpBoardEntities().map(task=>task.id);
assert(!boardIds.includes('board-done'), 'hiding completed tasks filters only the done status on the board');
assert(boardIds.includes('board-open') && boardIds.includes('board-cancelled'), 'open and cancelled tasks remain visible when done tasks are hidden');
assert(boardIds.includes('board-open'), 'board visibility is independent of the separate all-tasks status filter');
const boardToolbar=api.wpBoardToolbar(api.wpBoardEntities());
assert(boardToolbar.includes('wp-filter-layout wp-board-filter-row') && boardToolbar.includes('wp-filter-main'), 'the board uses the shared filter-grid structure');
assert(boardToolbar.includes('Показать выполненные задачи') && boardToolbar.includes('aria-pressed="false"') && boardToolbar.includes('data-scope="board"'));
assert(boardToolbar.includes('data-wp-filter-menu="board"') && boardToolbar.includes('data-act="wpFilterMenu"') && boardToolbar.includes('data-scope="board"'), 'the board filter opens through the shared popup trigger');
assert(planner.includes("const WP_BOARD_FOCUS=[['all','Все'],['urgent','Срочные'],['overdue','Просроченные'],['today','Срок сегодня'],['noDeadline','Без срока'],['unplanned','Без даты начала']]"), 'the board offers all six reference focus options');
assert(!boardToolbar.includes('wp-filter-pills'), 'board focus choices are not separate pill buttons');
assert(planner.includes('${wpBoardToolbar(items)}${wpBoardSummary(items)}') && !planner.includes('<div class="wp-board-pin">'), 'the board summary flows statically under the toolbar with no sticky pin wrapper');
assert(planner.includes("view.querySelector('.wp-board-pin')") && planner.includes('stuck.getBoundingClientRect().height>view.clientHeight*.6'), 'the stickiness guard measures the tallest strip without locking scroll');
const probeBoard = { id: 'probe-5', name: 'Probe', code: 'P', taskSchemaVersion: api.schema, ownerEmail: '', projects: [], people: [], invites: [], occurrences: [], recurrenceRules: [],
  statuses: [{ key: 'open', name: 'В работе' }, { key: 'waiting', name: 'Ожидают' }, { key: 'done', name: 'Готово' }, { key: 'overdue', name: 'Просрочено' }, { key: 'st_probe_1', name: 'Новый' }],
  tasks: [{ id: 'pt1', title: 'T', homeType: 'personal', statusKey: 'st_probe_1', priorityKey: 'normal', order: 1 }] };
const keepBoard = state.board, keepBoards = state.boards, keepId = state.boardId;
const keepShow = api.WP.boardShowCompleted, keepFocus = api.WP.boardFocus, keepAllFilter = api.WP.allFilter;
state.board = probeBoard; state.boards = [probeBoard]; state.boardId = probeBoard.id;
api.WP.allFilter = 'all'; api.WP.boardShowCompleted = true; api.WP.boardFocus = 'all';
const probeMarkup = api.wpBoardView();
assert.strictEqual((probeMarkup.match(/wp-board-column/g) || []).length, 5, 'a dictionary status always adds a board lane');
assert(probeMarkup.includes('--wp-lanes:5'), 'board lanes share one row sized by the live status count');
assert(probeMarkup.includes('<b>Новый</b>'), 'a custom status lane carries its dictionary name');
assert.strictEqual((probeMarkup.match(/<header>/g) || []).length, 1, 'semantic lanes skip headers and leave naming to the KPI strip');
state.board = keepBoard; state.boards = keepBoards; state.boardId = keepId;
api.WP.allFilter = keepAllFilter; api.WP.boardShowCompleted = keepShow; api.WP.boardFocus = keepFocus;
assert(html.includes('width: 520, onClose: () => tasksRerender(true), html:'), 'closing the dictionaries modal always re-renders the board, even via Escape or backdrop click');
assert(planner.includes("is-all-day${layout.segments.length?' has-spans':''}") && plannerStyle[1].includes('.wp-week-grid.has-spans .wp-day-items{margin-top:3px}'), 'span weeks keep a span-like gap between multi-day rows and capsules');
assert(api.WP.calendarShowCompleted, 'the board toolbar keeps the completed calendar filter untouched');
api.actions.wpBoardFocus({dataset:{filter:'urgent'}});
assert.deepStrictEqual(Array.from(api.wpBoardEntities(),task=>task.id).sort(),['board-other','board-urgent'],'Urgent means high or critical priority on the board');
assert(api.wpBoardToolbar(api.wpBoardEntities()).includes('<b>Срочные</b>'), 'the board dropdown trigger displays the selected focus');
api.actions.wpBoardFocus({dataset:{filter:'overdue'}});
assert.deepStrictEqual(Array.from(api.wpBoardEntities(),task=>task.id),['board-overdue'],'Overdue excludes tasks without an outstanding past deadline');
api.actions.wpBoardFocus({dataset:{filter:'all'}});
api.wpSetBoardFilter('assignee','person:Morgan');
assert.deepStrictEqual(Array.from(api.wpBoardEntities(),task=>task.id),['board-other'],'the board executor selector filters the chosen person');
api.actions.wpBoardMineToggle();
assert.strictEqual(api.WP.boardMineOnly,true);
assert.strictEqual(api.WP.boardAssigneeFilter,'all','enabling My Tasks clears the board executor filter (reference behavior)');
assert.deepStrictEqual(Array.from(api.wpBoardEntities(),task=>task.id).sort(),['board-cancelled','board-open','board-overdue','board-urgent'].sort(),'My Tasks shows only the signed-in person’s board tasks');
api.actions.wpBoardMineToggle();
assert.deepStrictEqual(Array.from(api.wpBoardEntities(),task=>task.id).sort(),['board-cancelled','board-open','board-other','board-overdue','board-urgent'].sort(),'turning My Tasks off keeps the cleared executor filter');
api.wpResetBoardFilters();
assert.strictEqual(api.WP.boardAssigneeFilter,'all'); assert.strictEqual(api.WP.boardMineOnly,false); assert.strictEqual(api.WP.boardFocus,'all'); assert.strictEqual(api.WP.boardTagFilter,'all'); assert.strictEqual(api.WP.boardSort,'urgency'); assert.strictEqual(api.WP.boardSortDir,'desc');
const allToolbar=api.wpAllFilterToolbar();
assert(allToolbar.includes('wp-filter-layout wp-all-filter-row') && allToolbar.includes('wp-filter-main'), 'all tasks uses the same filter-grid structure');
assert(allToolbar.indexOf('wp-filter-assignee')<allToolbar.indexOf('wp-filter-project'), 'assignee and project filters keep the same order in both views');
assert(boardToolbar.indexOf('wp-filter-assignee')<boardToolbar.indexOf('wp-filter-project'));
assert(allToolbar.includes('data-act="wpFilterMenu"') && allToolbar.includes('data-scope="all"'), 'the task-list filter opens through the shared popup trigger');
assert(planner.includes("const WP_LIST_FILTERS=[['active','Активные'],['open','В работе'],['waiting','Ожидание'],['done','Выполненные'],['cancelled','Отменённые'],['all','Все']]"), 'the task list offers all six reference status filters');
assert(boardToolbar.includes('data-wp-board-filter="sort"') && boardToolbar.includes('data-wp-board-filter="direction"'), 'the board offers sort mode and direction controls');
assert(allToolbar.includes('data-wp-filter="assignee"'), 'the existing executor selector remains available');
assert(allToolbar.includes('data-wp-filter="sort"') && allToolbar.includes('data-wp-filter="direction"') && ['urgency','deadline','tag','title','scheduled','created'].every(value=>allToolbar.includes(`value="${value}"`)), 'the task list offers all six reference sorts with direction');
assert(!allToolbar.includes('value="assignee"'), 'the duplicate By Assignee sort is removed');
const sortBoardBefore=state.board,sortBoardIdBefore=state.boardId;
const sortPrefsBefore={allFilter:api.WP.allFilter,allAssigneeFilter:api.WP.allAssigneeFilter,allMineOnly:api.WP.allMineOnly,allProjectFilter:api.WP.allProjectFilter,allSort:api.WP.allSort,allSortDir:api.WP.allSortDir,allTagFilter:api.WP.allTagFilter,query:api.WP.query,boardHomeFilter:api.WP.boardHomeFilter,boardAssigneeFilter:api.WP.boardAssigneeFilter,boardMineOnly:api.WP.boardMineOnly,boardFocus:api.WP.boardFocus,boardTagFilter:api.WP.boardTagFilter,boardSort:api.WP.boardSort,boardSortDir:api.WP.boardSortDir,boardShowCompleted:api.WP.boardShowCompleted};
state.board={id:'sort-board',projects:[],people:['Алекс','Анна','Борис','Зоя'],invites:[],tasks:[
  {id:'sort-critical',title:'Критическая',homeType:'personal',assignee:'Зоя',tags:['Бета','Альфа'],deadlineAt:'2026-10-20',statusKey:'open',priorityKey:'critical',order:4},
  {id:'sort-high',title:'Высокая',homeType:'personal',assignee:'Анна',tags:['Гамма'],deadlineAt:'2026-10-18',statusKey:'open',priorityKey:'high',order:3},
  {id:'sort-normal-late',title:'Обычная поздняя',homeType:'personal',assignee:'Алекс',tags:[],deadlineAt:'2026-10-22',statusKey:'open',priorityKey:'normal',order:1},
  {id:'sort-normal-early',title:'Обычная ранняя',homeType:'personal',assignee:'Алекс',tags:['Альфа'],deadlineAt:'2026-10-17',statusKey:'open',priorityKey:'normal',order:2},
  {id:'sort-normal-no-deadline',title:'Без срока',homeType:'personal',assignee:'',tags:['Дельта'],deadlineAt:null,statusKey:'open',priorityKey:'normal',order:5},
  {id:'sort-done-critical',title:'Выполнена',homeType:'personal',assignee:'Борис',tags:['0'],deadlineAt:'2026-10-01',statusKey:'done',priorityKey:'critical',order:0}
],occurrences:[],recurrenceRules:[]};
state.boardId='sort-board';
const ensureCountBeforeTaskQuery=api.ensureBoardCount(); api.wpBaseTasks();
assert.strictEqual(api.ensureBoardCount(),ensureCountBeforeTaskQuery,'valid task boards skip the full normalization pass during ordinary list/calendar queries');
api.WP.query=''; api.WP.allFilter='all'; api.WP.allAssigneeFilter='all'; api.WP.allMineOnly=false; api.WP.allProjectFilter='all'; api.WP.allSort='urgency'; api.WP.allSortDir='desc'; api.WP.allTagFilter='all';
api.WP.boardHomeFilter='all'; api.WP.boardAssigneeFilter='all'; api.WP.boardMineOnly=false; api.WP.boardFocus='all'; api.WP.boardTagFilter='all'; api.WP.boardSort='urgency'; api.WP.boardSortDir='desc'; api.WP.boardShowCompleted=true;
const sortIds=(mode,dir)=>{api.wpSetAllViewFilter('sort',mode); if(dir) api.wpSetAllViewFilter('direction',dir); return Array.from(api.wpAllEntities(),task=>task.id);};
const urgencyDescOrder=['sort-done-critical','sort-critical','sort-high','sort-normal-late','sort-normal-early','sort-normal-no-deadline'];
assert.deepStrictEqual(sortIds('urgency','desc'),urgencyDescOrder,'the default sort orders by urgency without hidden status groups (reference behavior)');
assert.deepStrictEqual(sortIds('urgency','asc'),['sort-normal-late','sort-normal-early','sort-normal-no-deadline','sort-high','sort-done-critical','sort-critical'],'urgency direction reverses priority order');
assert.deepStrictEqual(sortIds('tag'),['sort-done-critical','sort-critical','sort-normal-early','sort-high','sort-normal-no-deadline','sort-normal-late'],'tag sorting uses the first alphabetical tag and puts untagged tasks last');
assert.deepStrictEqual(sortIds('deadline'),['sort-done-critical','sort-normal-early','sort-high','sort-critical','sort-normal-late','sort-normal-no-deadline'],'deadline sorting is chronological, with missing deadlines last');
assert.deepStrictEqual(sortIds('title'),['sort-normal-no-deadline','sort-done-critical','sort-high','sort-critical','sort-normal-late','sort-normal-early'],'title sorting is alphabetical and case-insensitive');
api.wpSetAllViewFilter('sort','urgency'); api.wpSetAllViewFilter('direction','desc');
const priorAccountForMineFilter=state.account; state.account={name:'Алекс',email:'alex@example.test'};
api.actions.wpAllFilter({dataset:{filter:'done'}});
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id),['sort-done-critical'],'the selected status filter is exclusive');
api.actions.wpAllMineToggle();
assert.strictEqual(api.WP.allFilter,'done');
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id),[],'My Tasks combines with the selected status (no done tasks assigned to Alex)');
api.actions.wpAllFilter({dataset:{filter:'active'}});
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id).sort(),['sort-normal-early','sort-normal-late'].sort(),'switching status retains the My Tasks filter');
api.actions.wpAllMineToggle();
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id).sort(),['sort-critical','sort-high','sort-normal-early','sort-normal-late','sort-normal-no-deadline'].sort(),'turning My Tasks off preserves the active status');
api.wpSetAllViewFilter('assignee','person:Анна');
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id),['sort-high'],'the executor selector filters independently from sorting');
api.actions.wpAllMineToggle();
assert.strictEqual(api.WP.allMineOnly,true);
assert.strictEqual(api.WP.allAssigneeFilter,'all','enabling My Tasks clears the executor filter (reference behavior)');
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id).sort(),['sort-normal-early','sort-normal-late'].sort(),'My Tasks shows only the signed-in person’s tasks');
api.actions.wpAllMineToggle();
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id).sort(),['sort-critical','sort-high','sort-normal-early','sort-normal-late','sort-normal-no-deadline'].sort(),'turning My Tasks off keeps the cleared executor filter');
api.wpSetAllViewFilter('assignee','all');
const doneTaskForCancelledFilter=state.board.tasks.find(task=>task.id==='sort-done-critical'); doneTaskForCancelledFilter.statusKey='cancelled';
api.actions.wpAllFilter({dataset:{filter:'cancelled'}});
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id),['sort-done-critical'],'the cancelled status filter selects cancelled tasks independently');
doneTaskForCancelledFilter.statusKey='done'; state.account=priorAccountForMineFilter; api.WP.allFilter='all';
assert(api.wpAllFilterToolbar().includes('data-wp-filter="assignee"'),'the employee selector remains independent from the selected sort');
api.wpSetAllViewFilter('assignee','person:Анна');
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id),['sort-high'],'the selected employee filter narrows tasks to that assignee');
api.wpSetAllViewFilter('assignee','all'); api.wpSetAllViewFilter('sort','assignee');
assert.strictEqual(api.WP.allSort,'urgency','the removed assignee-sort value safely falls back to urgency');
assert(!api.wpAllFilterToolbar().includes('value="assignee"'),'legacy or invalid state cannot restore the duplicate assignee sort');
api.wpSetAllViewFilter('sort','tag');
const tagToolbar=api.wpAllFilterToolbar();
assert(tagToolbar.includes('data-wp-filter="tag"') && tagToolbar.includes('Все теги') && tagToolbar.includes('Альфа'),'the tag filter is always available in the reference layout');
api.wpSetAllViewFilter('tag','tag:Альфа');
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id).sort(),['sort-critical','sort-normal-early'].sort(),'the selected tag filter matches tasks that contain the tag');
api.wpSetAllViewFilter('tag','untagged');
assert.deepStrictEqual(Array.from(api.wpAllEntities(),task=>task.id),['sort-normal-late'],'the tag filter can also select untagged tasks');
api.wpSetAllViewFilter('tag','all'); api.wpSetAllViewFilter('sort','deadline');
assert(api.wpAllFilterToolbar().includes('data-wp-filter="tag"'),'the tag selector stays visible for deadline sorting (reference behavior)');
api.wpSetAllViewFilter('sort','urgency'); api.wpSetAllViewFilter('direction','desc');
api.WP.boardSort='urgency'; api.WP.boardSortDir='desc';
api.wpSetAllViewFilter('sort','deadline');
assert.deepStrictEqual(Array.from(api.wpBoardEntities(),task=>task.id),urgencyDescOrder,'the selected task-list sort does not change board ordering');
api.wpSetAllViewFilter('sort','urgency'); api.wpSetAllViewFilter('direction','desc');
const targetedTaskSnapshot=api.wpCaptureTaskEntities({id:'sort-normal-late',entityType:'task'});
state.board.tasks.find(task=>task.id==='sort-normal-late').title='Временное изменение';
assert(api.wpRestoreTaskEntities(targetedTaskSnapshot));
assert.strictEqual(state.board.tasks.find(task=>task.id==='sort-normal-late').title,'Обычная поздняя','a focused task snapshot restores only the edited task');
const newOccurrenceRef={id:'sort-critical::2026-10-23',entityType:'occurrence',seriesTaskId:'sort-critical',originalDate:'2026-10-23'};
const occurrenceSnapshot=api.wpCaptureTaskEntities(newOccurrenceRef,{includeRules:true});
state.board.occurrences.push({id:newOccurrenceRef.id,seriesTaskId:newOccurrenceRef.seriesTaskId,originalDate:newOccurrenceRef.originalDate,scheduledDate:newOccurrenceRef.originalDate,statusKey:'open'});
state.board.recurrenceRules.push({id:'temporary-rule'});
assert(api.wpRestoreTaskEntities(occurrenceSnapshot));
assert.strictEqual(state.board.occurrences.some(record=>record.id===newOccurrenceRef.id),false,'rollback removes an occurrence materialized by a date edit');
assert.strictEqual(state.board.recurrenceRules.some(rule=>rule.id==='temporary-rule'),false,'focused snapshots also restore recurrence-rule changes when requested');
api.wpSetAllViewFilter('sort','tag');
assert.strictEqual(api.WP.allSort,'tag','the sort control updates the selected task-list mode');
assert.strictEqual(api.WP.allSortDir,'asc','switching sort resets direction to the reference initial value');
assert(api.wpAllFilterToolbar().includes('data-wp-filter="sort"') && api.wpAllFilterToolbar().includes('<option value="tag" selected>'),'the selected sort remains visible in the native select control');
api.wpSetAllViewFilter('direction','desc');
assert.strictEqual(api.WP.allSortDir,'desc','the direction control updates the selected task-list direction');
api.wpSetAllViewFilter('sort','unknown');
assert.strictEqual(api.WP.allSort,'urgency','unsupported sort values fall back to urgency');
api.WP.allSort='deadline'; api.WP.allSortDir='asc';
assert.deepStrictEqual(Array.from(api.wpAllEntities({ignoreFilters:true}),task=>task.id),['sort-critical','sort-high','sort-normal-late','sort-normal-early','sort-normal-no-deadline','sort-done-critical'],'unfiltered task retrieval preserves insertion order for views that reuse it');
api.WP.allSort='invalid'; api.wpEnsureState();
assert.strictEqual(api.WP.allSort,'urgency','state validation rejects unsupported saved sort values');
api.WP.allSort='assignee'; api.wpEnsureState();
assert.strictEqual(api.WP.allSort,'urgency','state validation removes the obsolete assignee sort from saved state');
api.WP.allSort='tag'; api.WP.allTagFilter='tag:missing'; api.wpEnsureState();
assert.strictEqual(api.WP.allTagFilter,'all','state validation clears a tag filter that no longer exists');
api.WP.allSort='urgency'; api.WP.allTagFilter='tag:Альфа'; api.wpEnsureState();
assert.strictEqual(api.WP.allTagFilter,'tag:Альфа','the tag filter stays independent from the selected sort (reference behavior)');
api.WP.allSortDir='invalid'; api.WP.boardSortDir='invalid'; api.wpEnsureState();
assert.strictEqual(api.WP.allSortDir,'desc','state validation restores the urgency direction default');
assert.strictEqual(api.WP.boardSortDir,'desc','state validation restores the board direction default');
api.WP.allAssigneeFilter='mine'; api.WP.allMineOnly=false; api.wpEnsureState();
assert.strictEqual(api.WP.allAssigneeFilter,'all'); assert.strictEqual(api.WP.allMineOnly,true,'legacy My Tasks state migrates without occupying the executor filter');
api.WP.boardAssigneeFilter='mine'; api.WP.boardMineOnly=false; api.wpEnsureState();
assert.strictEqual(api.WP.boardAssigneeFilter,'all'); assert.strictEqual(api.WP.boardMineOnly,true,'legacy board My Tasks state migrates independently');
api.WP.allProjectFilter='project:missing'; api.WP.boardHomeFilter='project:missing'; api.WP.boardAssigneeFilter='person:missing'; api.WP.boardFocus='unknown'; api.wpEnsureState();
assert.strictEqual(api.WP.allProjectFilter,'all','a removed or inaccessible project cannot leave an invisible task-list filter active');
assert.strictEqual(api.WP.boardHomeFilter,'all','a deleted project cannot leave an invisible board filter active');
assert.strictEqual(api.WP.boardAssigneeFilter,'all','a removed person cannot leave an invisible board filter active');
assert.strictEqual(api.WP.boardFocus,'all','invalid board focus values return to the visible All state');
api.WP.allFilter='done'; api.WP.allAssigneeFilter='person:Анна'; api.WP.allMineOnly=true; api.WP.allProjectFilter='personal'; api.WP.allSort='tag'; api.WP.allSortDir='asc'; api.WP.allTagFilter='tag:Альфа'; api.WP.query='поиск';
api.wpResetAllFilters(false);
assert.strictEqual(api.WP.allFilter,'active'); assert.strictEqual(api.WP.allAssigneeFilter,'all'); assert.strictEqual(api.WP.allMineOnly,false); assert.strictEqual(api.WP.allProjectFilter,'all'); assert.strictEqual(api.WP.allSort,'urgency'); assert.strictEqual(api.WP.allSortDir,'desc'); assert.strictEqual(api.WP.allTagFilter,'all'); assert.strictEqual(api.WP.query,'','reset restores every task-list filter and the default sort');
state.board=sortBoardBefore; state.boardId=sortBoardIdBefore; Object.assign(api.WP,sortPrefsBefore);
api.WP.allFilter='active'; api.WP.boardShowCompleted=true; state.board.tasks=priorBoardTasks; state.board.occurrences=priorOccurrences;

const persistenceState={board:state.board,boards:state.boards,boardsMeta:state.boardsMeta,boardId:state.boardId,pendingKeys:state.taskMigrationPendingKeys};
const priorStoredBoard=api.store.get('shelter:board'),priorStoredBoards=api.store.get('shelter:boards'),saveCountBefore=api.saveNowCount();
state.board={id:'persist-test',taskSchemaVersion:api.schema,tasks:[{id:'date-persist',title:'Дата до сохранения',homeType:'personal',scheduledDate:'2026-10-20',deadlineAt:'2026-10-30',statusKey:'open',priorityKey:'normal',tags:[],order:1}],projects:[],people:[],invites:[],occurrences:[],recurrenceRules:[]};
state.boards=[state.board]; state.boardsMeta={retained:true}; state.boardId='persist-test'; state.taskMigrationPendingKeys=[];
assert.strictEqual(api.wpPersistBoard(),true,'task-board persistence verifies both board records');
assert.strictEqual(api.saveNowCount(),saveCountBefore,'saving a task board does not serialize unrelated application state');
assert.strictEqual(JSON.parse(api.wpStoreDecrypt(api.store.get('shelter:board'))).id,'persist-test');
assert.strictEqual(JSON.parse(api.wpStoreDecrypt(api.store.get('shelter:boards'))).retained,true);
assert(api.store.get('shelter:board').startsWith('ss1:'), 'persisted boards are encrypted at rest');
const dateRef={id:'date-persist',entityType:'task'}, dateCommitOptions={refresh:false,captureState:()=>api.wpCaptureTaskEntities(dateRef),restoreState:api.wpRestoreTaskEntities};
assert.strictEqual(api.wpCommit('Дата перенесена',()=>api.wpApplySchedule(dateRef,'2026-10-25'),dateCommitOptions),true,'date edits persist through the targeted task snapshot path');
assert.strictEqual(state.board.tasks[0].scheduledDate,'2026-10-25');
assert.strictEqual(JSON.parse(api.wpStoreDecrypt(api.store.get('shelter:board'))).tasks[0].scheduledDate,'2026-10-25');
assert.strictEqual(api.saveNowCount(),saveCountBefore,'a date edit does not invoke the global application serializer');
const storedBoardBeforeFailedDate=api.store.get('shelter:board'),storedBoardsBeforeFailedDate=api.store.get('shelter:boards');
context.__failNextStoreKey='shelter:boards';
assert.strictEqual(api.wpCommit('Дата перенесена',()=>api.wpApplySchedule(dateRef,'2026-10-26'),dateCommitOptions),false,'a failed registry write rejects the date edit');
assert.strictEqual(state.board.tasks[0].scheduledDate,'2026-10-25','a failed date save restores the task in memory');
assert.strictEqual(api.store.get('shelter:board'),storedBoardBeforeFailedDate,'a failed two-key save restores the original board record');
assert.strictEqual(api.store.get('shelter:boards'),storedBoardsBeforeFailedDate,'a failed two-key save restores the original board registry');
state.board=persistenceState.board; state.boards=persistenceState.boards; state.boardsMeta=persistenceState.boardsMeta; state.boardId=persistenceState.boardId; state.taskMigrationPendingKeys=persistenceState.pendingKeys;
if(priorStoredBoard==null) api.store.del('shelter:board'); else api.store.set('shelter:board',priorStoredBoard);
if(priorStoredBoards==null) api.store.del('shelter:boards'); else api.store.set('shelter:boards',priorStoredBoards);

const statusStateBefore={board:state.board,boards:state.boards,boardsMeta:state.boardsMeta,boardId:state.boardId,taskMigrationOK:state.taskMigrationOK};
const statusBoardRaw=api.store.get('shelter:board'),statusBoardsRaw=api.store.get('shelter:boards');
const allFilterBefore=api.WP.allFilter,boardCompletedBefore=api.WP.boardShowCompleted,undoTokenBefore=api.WP._undoToken;
state.board={id:'status-rerender-board',name:'Status refresh',code:'STATUS',taskSchemaVersion:api.schema,ownerEmail:'',projects:[],people:[],invites:[],occurrences:[],recurrenceRules:[],tasks:[{id:'status-rerender-task',title:'Статус меняет группу и фильтр',homeType:'personal',homeId:'',projectId:'',statusKey:'open',status:'В работе',priorityKey:'normal',prio:'Не срочные',prevPrio:'Не срочные',scheduledDate:'2026-10-20',deadlineAt:null,createdAt:'2026-10-01T00:00:00.000Z',updatedAt:'2026-10-01T00:00:00.000Z',completedAt:null,tags:[],recurrenceRuleId:null,order:1}]};
state.boards=[state.board];state.boardsMeta={};state.boardId=state.board.id;state.taskMigrationOK=true;
api.WP.allFilter='active';api.WP.boardShowCompleted=true;
assert.strictEqual(api.wpAllEntities().length,1,'an open task appears in the active list before the status update');
const lanesBefore=api.wpBoardEntities();
assert.strictEqual(lanesBefore.filter(task=>task.statusKey==='open').length,1);
assert.strictEqual(lanesBefore.filter(task=>task.statusKey==='done').length,0);
assert(api.wpBoardSummary(lanesBefore).includes('<span>В работе</span><b>1</b>'));
assert(api.wpBoardSummary(lanesBefore).includes('<span>Готово</span><b>0</b>'));
let statusViewRenders=0;const originalStatusRenderer=context.wpRenderView;
context.wpRenderView=()=>{statusViewRenders++;};
api.wpSetStatusRef({id:'status-rerender-task',entityType:'task'},'done');
context.wpRenderView=originalStatusRenderer;
assert.strictEqual(statusViewRenders,1,'a status edit rerenders the active planner view');
assert.strictEqual(api.wpAllEntities().length,0,'a completed task leaves the active filter immediately');
api.WP.allFilter='done';
assert.strictEqual(api.wpAllEntities().length,1,'the completed task appears in the done filter immediately');
const lanesAfter=api.wpBoardEntities();
assert.strictEqual(lanesAfter.filter(task=>task.statusKey==='open').length,0);
assert.strictEqual(lanesAfter.filter(task=>task.statusKey==='done').length,1);
assert(api.wpBoardSummary(lanesAfter).includes('<span>В работе</span><b>0</b>'));
assert(api.wpBoardSummary(lanesAfter).includes('<span>Готово</span><b>1</b>'));
state.board=statusStateBefore.board;state.boards=statusStateBefore.boards;state.boardsMeta=statusStateBefore.boardsMeta;state.boardId=statusStateBefore.boardId;state.taskMigrationOK=statusStateBefore.taskMigrationOK;
api.WP.allFilter=allFilterBefore;api.WP.boardShowCompleted=boardCompletedBefore;api.WP._undoToken=undoTokenBefore;
if(statusBoardRaw==null) api.store.del('shelter:board'); else api.store.set('shelter:board',statusBoardRaw);
if(statusBoardsRaw==null) api.store.del('shelter:boards'); else api.store.set('shelter:boards',statusBoardsRaw);

// Board statuses, task statuses, and board columns share one dictionary.
const customStatusDict = [{ key: 'open', name: 'Active' }, { key: 'st-review', name: 'Review' }, { key: 'done', name: 'Done' }];
const customStatusTasks = [
  { id: 'dict-open', title: 'Обычная', homeType: 'personal', statusKey: 'open', priorityKey: 'normal', order: 1 },
  { id: 'dict-custom', title: 'На проверке', homeType: 'personal', statusKey: 'st-review', priorityKey: 'normal', order: 2 },
  { id: 'dict-cancelled', title: 'Старая отменённая', homeType: 'personal', statusKey: 'cancelled', priorityKey: 'normal', order: 3 }
];
assert.deepStrictEqual([...api.wpNormalizeStatusesArray(['В работе', 'Пауза', 'Готово'])].map(s => s.key), ['open', 'waiting', 'done'], 'legacy status names map to stable keys');
assert.deepStrictEqual([...api.wpNormalizeStatusesArray(['В работе', 'На паузе'])].map(s => [s.key, s.name]), [['open', 'В работе'], ['st-на-паузе', 'На паузе'], ['done', 'Готово']], 'unknown legacy names keep deterministic keys and gain the structural done status');
assert.deepStrictEqual([...api.wpNormalizeBoard({ id: 'mig-status', statuses: ['В работе', 'Пауза', 'Готово'], tasks: [] }).statuses].map(s => s.key), ['open', 'waiting', 'done', 'overdue'], 'untouched boards upgrade to the new default statuses');
const dictStatusesBefore = state.board.statuses;
state.board.statuses = customStatusDict;
assert.strictEqual(api.wpTaskStatus({ statusKey: 'st-review' }), 'st-review', 'custom dictionary keys stay valid task statuses');
assert.strictEqual(api.wpTaskStatus({ status: 'Review' }), 'st-review', 'label-only tasks resolve through the dictionary');
assert.strictEqual(api.wpStatusLabel('st-review'), 'Review');
assert.strictEqual(api.wpDefaultStatusKey(), 'open');
const dictOptions = api.wpStatusOptions('open');
assert(dictOptions.includes('<option value="open" selected>Active</option>') && dictOptions.includes('<option value="st-review" >Review</option>'), 'the task status select follows the board dictionary');
assert(api.wpStatusOptions('cancelled').includes('<option value="cancelled" selected>Отменена</option>'), 'a legacy status in use stays selectable through the fallback label');
assert.deepStrictEqual([...api.wpBoardColumns(customStatusTasks)].map(c => c[0]), ['open', 'st-review', 'done', 'cancelled'], 'board columns follow the dictionary and keep a lane for legacy statuses in use');
assert.strictEqual([...api.wpBoardColumns(customStatusTasks)][3][1], 'Отменена');
state.board.statuses = [{ key: 'st-only', name: 'Solo' }, { key: 'done', name: 'Done' }];
assert.strictEqual(api.wpDefaultStatusKey(), 'st-only', 'new tasks fall back to the first status when open is gone');
state.board.statuses = dictStatusesBefore;
assert(html.includes("arr.push(wpMakeBoardStatus(r.name, r.flags, S.board));"), 'board settings add statuses through the shared status factory');
assert(html.includes('function wpStatusDlg(') && html.includes('data-ws-flag="done"') && html.includes('data-ws-flag="noRemind"') && html.includes('data-ws-flag="outdated"'), 'statuses gain an editor with the behavior catalog');
assert(html.includes('data-act="wpBoardMoveToggle"') && html.includes('data-act="wpBoardColMove"'), 'board lanes reorder through an explicit move mode');
assert(planner.includes("next=wpStatusClosed(current)?wpDefaultStatusKey():'done'"), 'toggling a closed-by-logic task returns it to work');
assert(planner.includes("if(!t || wpTaskQuiet(t) || !t.deadlineAt) return false;"), 'quiet statuses never go overdue');
assert(html.includes('Статус «Готово» нужен для учёта выполненных задач'), 'the structural done status cannot be deleted');
assert(planner.includes('columns=wpBoardColumns(items)'), 'the board renders its lanes from the shared dictionary');

// New statuses adopt canonical keys and fill their own lane instead of duplicating the edge fallback lane.
const adoptBefore = state.board.statuses;
state.board.statuses = [{ key: 'open', name: 'В работе' }, { key: 'done', name: 'Готово' }];
assert.strictEqual(api.wpStatusKeyForNewName('Отменена'), 'cancelled', 'a known status name adopts its canonical key and fills its own lane');
assert(api.wpStatusKeyForNewName('На проверке').startsWith('st'), 'custom names keep unique generated keys');
assert.strictEqual(api.wpStatusKeyForNewName(''), '');
state.board.statuses = [{ key: 'open', name: 'В работе' }, { key: 'cancelled', name: 'Отменена' }, { key: 'done', name: 'Готово' }];
assert(api.wpStatusKeyForNewName('Отменена').startsWith('st'), 'a taken canonical key falls back to a unique lane');
const madeStatus = JSON.parse(JSON.stringify(api.wpMakeBoardStatus('X', { done: true, noRemind: false, outdated: false }, null)));
assert(madeStatus.key.startsWith('st') && madeStatus.name === 'X');
assert.deepStrictEqual(madeStatus.flags, { done: true, noRemind: false, outdated: false });
const keptStatus = JSON.parse(JSON.stringify(api.wpMakeBoardStatus('Y', null, null, 'st-keep')));
assert.strictEqual(keptStatus.key, 'st-keep', 'renaming a status keeps its lane identity');
assert.strictEqual('flags' in keptStatus, false, 'statuses without behaviors stay clean key/name pairs');
state.board.statuses = adoptBefore;

// Label-only tasks resolve to the board lane first, not the global label map.
const precBefore = state.board.statuses;
state.board.statuses = [{ key: 'st-pause', name: 'Пауза' }, { key: 'open', name: 'В работе' }, { key: 'done', name: 'Готово' }];
assert.strictEqual(api.wpTaskStatus({ status: 'Пауза' }), 'st-pause', 'label-only tasks resolve to the board lane first, not the global label map');
assert.strictEqual(api.wpTaskStatus({ status: 'Ожидают' }), 'waiting', 'unknown labels keep resolving through the global map');
state.board.statuses = precBefore;

// Status behaviors drive task logic the way CRM status categories do.
const normFlags = JSON.parse(JSON.stringify([...api.wpNormalizeStatusesArray([{ key: 'st-x', name: 'X', flags: { done: true, junk: 1 } }, { key: 'open', name: 'В работе' }])]));
assert.deepStrictEqual(normFlags[0].flags, { done: true, noRemind: false, outdated: false }, 'normalization preserves behaviors and drops unknown flags');
assert.strictEqual('flags' in normFlags[1], false);
const flagStatusesBefore = state.board.statuses;
state.board.statuses = [
  { key: 'open', name: 'В работе' },
  { key: 'st-shipped', name: 'Отгружено', flags: { done: true, noRemind: false, outdated: false } },
  { key: 'st-idle', name: 'Отложено', flags: { done: false, noRemind: true, outdated: false } },
  { key: 'st-junk', name: 'Мусор', flags: { done: false, noRemind: false, outdated: true } },
  { key: 'done', name: 'Готово' }
];
assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpStatusFlags('st-shipped'))), { done: true, noRemind: false, outdated: false });
assert.strictEqual(api.wpStatusDone('st-shipped'), true);
assert.strictEqual(api.wpStatusDone('done'), true, 'the structural done status keeps its implicit logic');
assert.strictEqual(api.wpStatusOutdated('cancelled'), true, 'the legacy cancelled status keeps its implicit logic');
assert.strictEqual(api.wpStatusQuiet('st-idle'), true);
assert.strictEqual(api.wpStatusClosed('st-junk'), true);
assert.strictEqual(api.wpTaskDone({ statusKey: 'st-shipped' }), true);
assert.strictEqual(api.wpTaskOutdated({ statusKey: 'st-junk' }), true);
assert.strictEqual(api.wpTaskClosed({ statusKey: 'open' }), false);
const shippedTask = { id: 'flag-shipped', title: 'Отгружена', homeType: 'personal', statusKey: 'st-shipped', priorityKey: 'normal', deadlineAt: '2000-01-01', order: 1 };
const idleTask = { id: 'flag-idle', title: 'Отложена', homeType: 'personal', statusKey: 'st-idle', priorityKey: 'normal', deadlineAt: '2000-01-01', order: 2 };
const junkTask = { id: 'flag-junk', title: 'Мусорная', homeType: 'personal', statusKey: 'st-junk', priorityKey: 'normal', order: 3 };
const openTask = { id: 'flag-open', title: 'Обычная', homeType: 'personal', statusKey: 'open', priorityKey: 'normal', order: 4 };
assert.strictEqual(api.wpDeadlineOverdue(shippedTask), false, 'done-like tasks never go overdue');
assert.strictEqual(api.wpDeadlineOverdue(idleTask), false, 'quiet tasks never go overdue');
assert.strictEqual(api.wpDeadlineOverdue({ statusKey: 'open', deadlineAt: '2000-01-01' }), true);
assert(api.wpBoardSummary([shippedTask, idleTask]).includes('<span>Готово</span><b>1</b>'), 'the board summary counts done-like tasks as completed');
assert(api.wpTaskCard(shippedTask, { compact: true }).includes('is-done'), 'done-like cards render completed');
assert(api.wpTaskCard(junkTask, { compact: true }).includes('is-cancelled'), 'outdated cards render dimmed');
const flagTasksBefore = state.board.tasks, flagOccBefore = state.board.occurrences, flagFilterBefore = api.WP.allFilter, flagQueryBefore = api.WP.query;
const flagAssigneeBefore = api.WP.allAssigneeFilter, flagTagBefore = api.WP.allTagFilter, flagHomeBefore = api.WP.allProjectFilter, flagSortBefore = api.WP.allSort, flagDirBefore = api.WP.allSortDir;
state.board.tasks = [shippedTask, idleTask, junkTask, openTask]; state.board.occurrences = [];
api.WP.query = ''; api.WP.allAssigneeFilter = 'all'; api.WP.allTagFilter = 'all'; api.WP.allProjectFilter = 'all'; api.WP.allSort = 'urgency'; api.WP.allSortDir = 'desc';
api.WP.allFilter = 'active';
assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpAllEntities().map(t => t.id))), ['flag-idle', 'flag-open'], 'outdated and done-like tasks leave the active list, quiet tasks stay');
api.WP.allFilter = 'done';
assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpAllEntities().map(t => t.id))), ['flag-shipped'], 'the done filter includes done-like tasks');
api.WP.allFilter = 'cancelled';
assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpAllEntities().map(t => t.id))), ['flag-junk'], 'the cancelled filter includes outdated tasks');
state.board.tasks = flagTasksBefore; state.board.occurrences = flagOccBefore; api.WP.allFilter = flagFilterBefore; api.WP.query = flagQueryBefore;
api.WP.allAssigneeFilter = flagAssigneeBefore; api.WP.allTagFilter = flagTagBefore; api.WP.allProjectFilter = flagHomeBefore; api.WP.allSort = flagSortBefore; api.WP.allSortDir = flagDirBefore;
state.board.statuses = flagStatusesBefore;

// Board lanes reorder only in the explicit move mode.
const moveStatusesBefore = state.board.statuses;
state.board.statuses = [{ key: 'open', name: 'В работе' }, { key: 'waiting', name: 'Ожидают' }, { key: 'done', name: 'Готово' }];
assert.strictEqual(api.wpMoveBoardStatus('waiting', -1), true);
assert.deepStrictEqual(state.board.statuses.map(s => s.key), ['waiting', 'open', 'done']);
assert.strictEqual(api.wpMoveBoardStatus('waiting', -1), false, 'the first lane cannot move further left');
assert.strictEqual(api.wpMoveBoardStatus('done', 1), false, 'the last lane cannot move further right');
assert.strictEqual(api.wpMoveBoardStatus('missing', 1), false);
assert.deepStrictEqual([...api.wpBoardColumns([])].map(c => c[0]), ['waiting', 'open', 'done'], 'lanes follow the reordered dictionary');
state.board.statuses = moveStatusesBefore;
assert(api.wpBoardLaneHeader('open', 'В работе', 2, false, 0, 3).includes('<header><div><b>В работе</b></div><span>2</span></header>'), 'calm lane headers keep their exact markup');
assert(api.wpBoardLaneHeader('open', 'В работе', 2, true, 0, 3).includes('data-act="wpBoardColMove"'), 'move mode adds lane arrows');
const moveModeBefore = api.WP.boardMoveColumns, moveTasksBefore = state.board.tasks, moveOccBefore2 = state.board.occurrences;
const moveQueryBefore = api.WP.query, moveFocusBefore = api.WP.boardFocus, moveShowBefore = api.WP.boardShowCompleted, moveMineBefore = api.WP.boardMineOnly;
const moveHomeBefore = api.WP.boardHomeFilter, moveAssigneeBefore = api.WP.boardAssigneeFilter, moveTagBefore = api.WP.boardTagFilter;
state.board.statuses = [{ key: 'open', name: 'В работе' }, { key: 'st-x', name: 'Своя' }, { key: 'done', name: 'Готово' }];
state.board.tasks = []; state.board.occurrences = [];
api.WP.query = ''; api.WP.boardFocus = 'all'; api.WP.boardShowCompleted = true; api.WP.boardMineOnly = false;
api.WP.boardHomeFilter = 'all'; api.WP.boardAssigneeFilter = 'all'; api.WP.boardTagFilter = 'all';
api.WP.boardMoveColumns = true;
const moveMarkup = api.wpBoardView();
assert.strictEqual((moveMarkup.match(/data-act="wpBoardColMove"/g) || []).length, 6, 'every dictionary lane offers both move buttons in move mode');
assert(moveMarkup.includes('data-dir="-1"') && moveMarkup.includes('disabled'), 'edge lanes disable the outward move button');
api.WP.boardMoveColumns = false;
const calmMarkup = api.wpBoardView();
assert(!calmMarkup.includes('wpBoardColMove'), 'move buttons disappear outside move mode');
assert.strictEqual((calmMarkup.match(/<header>/g) || []).length, 1, 'semantic lanes keep skipping headers outside move mode');
state.board.statuses = moveStatusesBefore; state.board.tasks = moveTasksBefore; state.board.occurrences = moveOccBefore2;
api.WP.query = moveQueryBefore; api.WP.boardFocus = moveFocusBefore; api.WP.boardShowCompleted = moveShowBefore; api.WP.boardMineOnly = moveMineBefore;
api.WP.boardHomeFilter = moveHomeBefore; api.WP.boardAssigneeFilter = moveAssigneeBefore; api.WP.boardTagFilter = moveTagBefore; api.WP.boardMoveColumns = moveModeBefore;
const dayCapsule = api.wpDayCapsule({ id: 'cap-1', title: 'Капсула', homeType: 'personal', statusKey: 'open', priorityKey: 'high', deadlineAt: '2026-10-15', tags: [] });
assert(dayCapsule.includes('wp-day-capsule p-high') && dayCapsule.includes('data-task-id="cap-1"') && dayCapsule.includes('data-act="taskEdit"'), 'single-day tasks render as thin capsules that open the editor');
const dayCapsuleMeta = api.wpDayCapsule({ id: 'cap-2', title: 'Мета', homeType: 'personal', statusKey: 'open', priorityKey: 'normal', assignee: 'Владимир', deadlineAt: '2026-10-20', tags: [] });
assert(dayCapsuleMeta.includes('<small>Владимир · До: 20.10</small>'), 'day capsules show the assignee and deadline in one meta line');

api.WP.monthStart = '2026-10-01'; api.WP.mobileDate = '2026-10-01'; api.WP.calendarMode = 'month';
const monthView = api.wpViewHtml();
assert(!monthView.includes('wp-month-span-layer') && !monthView.includes('wp-calendar-span--month'), 'the month grid renders dots without span rows');
const rangeMonthWeek = monthView.slice(monthView.indexOf('<div class="wp-month-grid"'), monthView.indexOf('wp-month-selected'));
assert.strictEqual(countId(rangeMonthWeek, 'span-before-month'), 0, 'a range starting before the month grid leaves no row');
assert(rangeMonthWeek.includes('wp-month-dots'), 'covered month cells keep priority dots');
assert(monthView.includes('wp-month-selected'), 'the month keeps its selected-day panel next to the dot grid');
api.WP.weekStart = '2026-10-12'; api.WP.mobileDate = '2026-10-15'; api.WP.calendarMode = 'week';
const rangeWeekMarkup = api.wpViewHtml();
assert(rangeWeekMarkup.includes('wp-week-grid is-all-day') && rangeWeekMarkup.includes('wp-week-span-layer'));
assert(!rangeWeekMarkup.includes('Входящие без плана') && !rangeWeekMarkup.includes('wp-inbox-shelf'), 'the week view replaces the unplanned inbox shelf');
const weekSelectedAt = rangeWeekMarkup.indexOf('<section class="wp-week-selected"');
assert(weekSelectedAt >= 0, 'the week view shows a selected-day list below the calendar');
const weekSelectedPanel = rangeWeekMarkup.slice(weekSelectedAt, rangeWeekMarkup.indexOf('</section>', weekSelectedAt) + '</section>'.length);
assert(weekSelectedPanel.includes('month-span-a') && weekSelectedPanel.includes('month-span-b') && weekSelectedPanel.includes('single-day-task'), 'the week selected-day list includes every task covering that date, including multi-day tasks');
assert(rangeWeekMarkup.includes('Однодневная задача') && rangeWeekMarkup.includes('Задача на несколько дней'));
const desktopWeekGrid = rangeWeekMarkup.slice(rangeWeekMarkup.indexOf('<div class="wp-week-grid is-all-day'), weekSelectedAt);
assert.strictEqual(countId(desktopWeekGrid, 'week-span'), 1, 'the desktop week grid displays a range as one horizontal row');
assert.strictEqual(countId(desktopWeekGrid, 'month-span-a'), 1);
assert.strictEqual(countId(desktopWeekGrid, 'single-day-task', 'data-task-id'), 1);
assert(rangeWeekMarkup.includes('Многодневные задачи на весь день'), 'multi-day tasks remain date-only and use no invented time slots');
assert(rangeWeekMarkup.includes('data-act="wpCalendarToggleCompleted"') && rangeWeekMarkup.includes('Скрыть выполненные задачи'), 'week calendar offers the completed-task visibility toggle');
api.wpSelectCalendarDate('2026-10-16');
assert.strictEqual(api.WP.mobileDate, '2026-10-16', 'selecting a day updates the week’s bottom list target');
assert.strictEqual(api.WP.weekStart, '2026-10-12');
api.WP.mobileDate = '2026-10-15';
assert(rangeWeekMarkup.includes('data-act="wpQuickDay"'), 'week days retain their own add-task controls');
api.WP.view = 'week';
assert(api.wpHeaderControls().includes('data-act="wpQuickGlobal"'), 'the global add button remains available in week mode');
assert(api.wpHeaderControls().includes('class="wp-mobile-menu"') && api.wpHeaderControls().includes('data-act="wpRailOpen"'), 'the task header exposes the mobile navigation opener');
api.WP.query='поиск';
const searchHeader=api.wpHeaderControls();
assert(/class="wp-search-clear"[^>]*aria-label="Очистить поиск"/.test(searchHeader) && /<kbd hidden>/.test(searchHeader), 'search shows a clear button only while a query is active');
api.WP.query='';
api.WP.calendarMode = 'month';
assert(api.wpHeaderControls().includes('data-act="wpQuickGlobal"'), 'the global add button remains available in month mode');
api.WP.view = 'inbox';
assert(api.wpHeaderControls().includes('data-act="wpQuickGlobal"'), 'the global add button remains available in non-calendar views');
api.WP.view = 'week';
api.WP.calendarMode = 'week';
assert.strictEqual(api.wpTaskScheduleEnd(rangeTasks[1]), '2026-10-13', 'the existing deadline is the calendar range end');
assert.strictEqual(api.wpTaskScheduleOffset(rangeTasks[1]), 3);
assert.strictEqual(api.wpTaskScheduleEnd({ scheduledDate: '2026-10-15', deadlineAt: '2026-10-14' }), '2026-10-15', 'a deadline before the plan does not reverse the calendar range');
assert.strictEqual(api.wpApplySchedule({ id: 'week-span', entityType: 'task' }, '2026-10-12'), true);
assert.strictEqual(state.board.tasks.find(task => task.id === 'week-span').scheduledDate, '2026-10-12');
assert.strictEqual(state.board.tasks.find(task => task.id === 'week-span').deadlineAt, '2026-10-13', 'rescheduling changes the plan but preserves its hard deadline');
assert.strictEqual(api.wpApplySchedule({ id: 'week-span', entityType: 'task' }, null), true);
assert.strictEqual(state.board.tasks.find(task => task.id === 'week-span').scheduledDate, null, 'clearing a plan does not erase the separate deadline');
assert.strictEqual(state.board.tasks.find(task => task.id === 'week-span').deadlineAt, '2026-10-13');

// Classification stays independent of a task's normal destination; creation has no status control, but editing does.
state.board.tasks = [];
const createdPersonal = api.wpCreateTask('Личная с меткой', '2026-10-13', null, 2, 'Я', false, 'personal');
assert(createdPersonal);
assert.strictEqual(createdPersonal.homeType, 'personal');
assert.strictEqual(createdPersonal.assignee, 'me', 'new tasks persist the self marker rather than a name copy');
assert.strictEqual(api.wpAssigneeLabel(createdPersonal), 'Alex', 'new tasks display the profile name');
assert.strictEqual(createdPersonal.matrixQuadrant, 2);
assert.strictEqual(createdPersonal.statusKey, 'open');
const projectTarget = api.wpContextAddTarget('projects', 'project-1');
assert.strictEqual(projectTarget.mode, 'editor', 'the “В проект” action opens the full task editor');
assert.strictEqual(projectTarget.projectId, 'project-1', 'the current project is passed into the task editor');
assert.strictEqual(projectTarget.homeType, 'project');
assert.strictEqual(api.wpContextAddTarget('projects', '__personal__').mode, 'editor', 'personal creation uses the full task editor');
assert.strictEqual(api.wpContextAddTarget('week', null, '2026-10-13').mode, 'editor', 'week creation uses the full task editor');
const createdProject = api.wpCreateTask('Задача проекта', '2026-10-14', 'project-1', 1, 'Alex', false, 'project');
assert(createdProject && createdProject.id);
assert.strictEqual(createdProject.homeType, 'project');
assert.strictEqual(createdProject.homeId, 'project-1');
assert.strictEqual(createdProject.projectId, 'project-1');
for (const field of ['description', 'tags', 'statusKey', 'priorityKey', 'scheduledDate', 'deadlineAt', 'assignee', 'createdAt', 'updatedAt']) {
  assert(Object.prototype.hasOwnProperty.call(createdProject, field), `project tasks keep the complete task model field ${field}`);
}
const taskCountBeforeInvalidProject = state.board.tasks.length;
assert.strictEqual(api.wpCreateTask('Не переносить в личные', null, 'missing-project', null, 'Alex', false, 'project'), false, 'a missing project is rejected instead of silently creating a personal task');
assert.strictEqual(state.board.tasks.length, taskCountBeforeInvalidProject, 'rejecting an invalid project does not create a task');
api.WP.quick = { targetDate: null, projectId: 'missing-project', homeType: 'project', assignee: 'Alex', quadrant: null, context: 'week', value: 'Не переносить в личные' };
api.wpQuickSubmit();
assert.strictEqual(state.board.tasks.length, taskCountBeforeInvalidProject, 'quick-add reports a missing project without persisting a task');
assert.strictEqual(api.WP.quick.value, 'Не переносить в личные', 'failed project quick-add preserves the title for recovery');
api.WP.quick = null;
const formValues = {
  title: 'Новая задача', kind: 'personal', projectId: '', assignee: 'Я', scheduledDate: '2026-10-13', deadlineAt: '2026-10-15',
  statusKey: 'open', priorityKey: 'normal', quadrant: '1', description: '', tags: '', durationEstimateMinutes: '',
  waitingReason: '', reviewAt: '', frequency: 'none', interval: 1, intervalDays: 1, weekdays: [1], mode: 'calendar', endDate: ''
};
const createForm = api.wpEditorBodyHtml(null, 'series', true, formValues, false, null);
const projectForm = api.wpEditorBodyHtml(null, 'series', true, { ...formValues, kind: 'project', projectId: 'project-1' }, false, null);
assert(projectForm.includes('id="wpProject"') && projectForm.includes('<option value="project-1" selected>'), 'the project task editor preselects the active project');
for (const field of ['wpTitle', 'wpProject', 'wpAssignee', 'wpScheduled', 'wpDeadline', 'wpPriority', 'wpDescription', 'wpTags']) {
  assert(projectForm.includes(`id="${field}"`), `project add opens the full editor field ${field}`);
}
assert(createForm.includes('<span>Дата начала</span>') && createForm.includes('<span>Крайний срок</span>') && !createForm.includes('Плановая дата'), 'the task form labels the schedule as its start date');
assert(createForm.includes('id="wpScheduled" type="date"') && createForm.includes('id="wpDeadline" type="date"'), 'task editor date fields match the reference native controls');
assert(planner.includes('const todayDefault=wpDateKey(Date.now())') && planner.includes('vals.scheduledDate=todayDefault') && planner.includes('vals.deadlineAt=wpDeadlineKey(opts.deadlineAt)||todayDefault'), 'new tasks default both start date and deadline to today');
const standardDateField = api.wpDateFieldHtml('standardDate', 'Дата', '2026-10-13');
assert(standardDateField.includes('wp-date-trigger') && standardDateField.includes('data-wp-date-open="standardDate"'), 'date controls outside the task editor retain their calendar button');
assert(planner.includes("input.matches('[data-wp-date-open]')?input:input.closest('.wp-date-control')?.querySelector('[data-wp-date-open]')"), 'icon-free date fields open the custom calendar from the focused input');
assert(createForm.includes('<option value="__add">+ Добавить новый проект</option>'), 'new-task project menu always offers project creation');
const projectsBeforeEmptyForm = state.board.projects;
state.board.projects = [];
const emptyProjectForm = api.wpEditorBodyHtml(null, 'series', true, { ...formValues, kind: 'project', projectId: '' }, false, null);
assert(emptyProjectForm.includes('id="wpProject"') && emptyProjectForm.includes('+ Добавить новый проект'), 'the add-project action remains available when the board has no projects');
state.board.projects = projectsBeforeEmptyForm;
assert(createForm.includes('id="wpStatus"') && !createForm.includes('option value="matrix"'), 'new tasks expose status selection without legacy Matrix options (reference behavior)');
assert(!createForm.includes('id="wpQuadrant"') && !createForm.includes('Метка Матрицы') && !createForm.includes('Матрица'), 'the task editor no longer exposes Matrix classification');
assert(!createForm.includes('wpScheduledEnd') && !createForm.includes('Дата окончания · для календаря') && createForm.includes('id="wpDeadline"'), 'the existing deadline is the only end-date field in the form');
assert(!createForm.includes('wpDestinationHint') && !createForm.includes('Задача будет добавлена'));
const existingTask = { id: 'existing-task', title: 'Уже создана', homeType: 'personal', assignee: 'Я', statusKey: 'waiting', priorityKey: 'normal', scheduledDate: null, tags: [], matrixQuadrant: null };
const editForm = api.wpEditorBodyHtml(existingTask, 'task', false, { ...formValues, title: existingTask.title, statusKey: 'waiting' }, false, existingTask);
assert(editForm.includes('id="wpStatus"'), 'editing an existing task must preserve status changes');
assert(editForm.includes('id="wpScheduled" type="date"') && editForm.includes('id="wpDeadline" type="date"'), 'editing-task date fields use the same reference native controls');
assert.strictEqual(api.wpTaskHomeType({ homeType: 'matrix' }), 'personal', 'stored Matrix homes read as personal tasks');
const legacyMatrixForm = api.wpEditorBodyHtml({ ...existingTask, homeType: 'matrix' }, 'task', false, { ...formValues, kind: 'matrix' }, false, { ...existingTask, homeType: 'matrix' });
assert(!legacyMatrixForm.includes('прежний список') && !legacyMatrixForm.includes('value="matrix"') && legacyMatrixForm.includes('>Личные<'), 'legacy Matrix tasks open as personal tasks without Matrix options');
const applied = { id: 'new-project-task', homeType: 'personal', statusKey: 'open', priorityKey: 'normal', tags: [], matrixQuadrant: null };
api.wpApplyTaskForm(applied, { ...formValues, title: 'Метка в проекте', kind: 'project', projectId: 'project-1', quadrant: 0, statusKey: 'open' }, { newTask: true });
assert.strictEqual(applied.homeType, 'project');
assert.strictEqual(applied.assignee, 'me', 'saving an alias from an older form stores the self marker');
assert.strictEqual(api.wpAssigneeLabel(applied), 'Alex');
assert.strictEqual(applied.matrixQuadrant, null, 'stale quadrant input from older forms is ignored');
const moveTask = { id: 'move-personal-project', title: 'Перенос', homeType: 'personal', homeId: '', projectId: '', project: '', assignee: 'Alex', statusKey: 'open', priorityKey: 'normal', tags: [] };
state.board.tasks.push(moveTask);
assert.strictEqual(api.wpApplyEntityHome({ id: 'move-personal-project', entityType: 'task' }, 'project', 'project-1'), true, 'personal tasks move into projects');
assert.strictEqual(api.wpTaskHomeType(moveTask), 'project');
assert.strictEqual(moveTask.projectId, 'project-1');
assert.strictEqual(api.wpApplyEntityHome({ id: 'move-personal-project', entityType: 'task' }, 'personal'), true, 'project tasks move back to personal tasks');
assert.strictEqual(api.wpTaskHomeType(moveTask), 'personal');
assert.strictEqual(moveTask.projectId, '');
state.board.tasks = state.board.tasks.filter(task => task.id !== 'move-personal-project');
assert.strictEqual(applied.statusKey, 'open');
assert.strictEqual(applied.deadlineAt, '2026-10-15');
assert(!Object.prototype.hasOwnProperty.call(applied, 'scheduledEndDate'), 'saving a task no longer writes a separate calendar end date');
assert(!editForm.includes('wpCommentThread'), 'private tasks do not show a shared comment thread');
const sharedTask = { id: 'shared-comment-task', title: 'Совместная задача', kind: 'project', homeType: 'project', projectId: 'project-1', assignee: 'Alex', comments: [] };
assert.strictEqual(api.wpCanCommentTask(sharedTask), true, 'visible project tasks support comments');
assert.strictEqual(api.wpCanCommentTask({ ...sharedTask, homeType: 'personal', kind: 'personal' }), false, 'private tasks do not support shared comments');
const savedComment = api.wpAddTaskComment(sharedTask, 'Проверил детали.\nВсё готово.');
assert(savedComment && savedComment.id && savedComment.createdAt);
assert.strictEqual(savedComment.body, 'Проверил детали.\nВсё готово.');
assert.strictEqual(savedComment.author, 'Alex');
assert.strictEqual(savedComment.projectId, 'project-1', 'comments are scoped to the project where they were posted');
assert(!Object.prototype.hasOwnProperty.call(savedComment, 'authorEmail'), 'comments do not copy account email into the task record');
sharedTask.comments.push({ id: 'other-project-comment', projectId: 'project-2', body: 'Private to another project', author: 'Member', createdAt: '2026-10-14T12:00:00.000Z' });
assert.strictEqual(api.wpTaskCommentList(sharedTask).length, 1, 'comments from another project are not exposed');
assert.strictEqual(api.wpTaskCommentList({ ...sharedTask, projectId: 'project-2' }).length, 1, 'comments stay scoped if a task moves between projects');
assert.strictEqual(api.wpAddTaskComment(sharedTask, '   '), null, 'empty comments are rejected');
assert.strictEqual(api.wpAddTaskComment(sharedTask, 'x'.repeat(5001)), null, 'comments are length-limited');
const xssComment = api.wpAddTaskComment(sharedTask, '<script>alert(1)</script>');
assert(xssComment);
const commentsMarkup = api.wpCommentThreadHtml(sharedTask);
assert(commentsMarkup.includes('Комментарии') && commentsMarkup.includes('Проверил детали.') && commentsMarkup.includes('maxlength="5000"'));
assert(!commentsMarkup.includes('<script>') && commentsMarkup.includes('&lt;script&gt;'), 'comment text is escaped before rendering');
const sharedForm = api.wpEditorBodyHtml(sharedTask, 'task', false, { ...formValues, kind: 'project', projectId: 'project-1', title: sharedTask.title }, false, sharedTask);
assert(sharedForm.includes('id="wpCommentThread"'), 'project-task editor includes a comment thread');
const sharedCard = api.wpTaskCard(sharedTask, { compact: true });
assert(sharedCard.includes('class="wp-comment-link"') && sharedCard.includes('data-open-comments="1"'), 'project cards provide direct access to their comments');
assert.strictEqual(api.wpDeadlineText({ deadlineAt: '2026-10-19', statusKey: 'open' }), 'До: 19.10.2026');
const longTitle = 'Подготовить подробный план запуска нового продукта и проверить все контрольные материалы';
const taskMarkup = api.wpTaskCard({ id: 'display-task', title: longTitle, homeType: 'personal', assignee: 'Alex', deadlineAt: '2026-10-19', statusKey: 'open', priorityKey: 'normal', tags: [], matrixQuadrant: null }, { compact: true });
assert(taskMarkup.includes('class="wp-task-title wp-title-marquee-clip"'));
assert(taskMarkup.includes(`<span class="wp-title-marquee-text">${longTitle}</span>`));
assert(taskMarkup.includes('До: 19.10.2026'));
assert(taskMarkup.includes('class="wp-task p-normal'), 'task cards expose the priority class used for urgency coloring');
assert.strictEqual(api.wpValidPriorityColor('#7c5cff'), '#7c5cff');
assert.strictEqual(api.wpValidPriorityColor('red'), '', 'only hex colors are accepted as board priority presets');
assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpBoardPriorityColors({ priorityColors: { high: '#12ab34', critical: 'red' } }))), { none: '', normal: '', high: '#12ab34', critical: '' }, 'board presets keep only valid hex colors per priority');
assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpBoardPriorityColors({}))), { none: '', normal: '', high: '', critical: '' }, 'boards without presets fall back to theme colors');
assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpDefaultPriorityColors())), { none: '#8a8f9e', normal: '#7c5cff', high: '#f5a524', critical: '#f31260' }, 'preset defaults resolve to fixed hex colors without a DOM');
assert.strictEqual(api.wpPriorityVarsStyle({ priorityColors: { normal: '#112233', high: '#445566' } }), '--wp-prio-normal:#112233;--wp-prio-high:#445566');
assert.strictEqual(api.wpPriorityVarsStyle({}), '');
assert.strictEqual(api.wpPriorityVarsAttr({ priorityColors: { critical: '#aabbcc' } }), ' style="--wp-prio-critical:#aabbcc"');
assert.strictEqual(api.wpPriorityVarsAttr({}), '');
const presetCard = api.wpTaskCard({ id: 'preset-color', title: 'Пресет доски', homeType: 'personal', assignee: 'Alex', statusKey: 'open', priorityKey: 'normal', priorityColor: '#12ab34', tags: [] }, { compact: true });
assert(presetCard.includes('wp-task p-normal') && !presetCard.includes('--wp-prio:'), 'task cards no longer carry per-task custom colors');
assert.strictEqual(api.wpMonthDots([{ priorityKey: 'high' }, { priorityKey: 'normal' }]), '<span class="wp-month-dots"><i class="p-high"></i><i class="p-normal"></i></span>', 'month cells show one priority dot per task');
assert.strictEqual(api.wpMonthDots([]), '', 'empty days render no dots');
assert.strictEqual(api.wpMonthDots([{ priorityKey: 'critical' }, { priorityKey: 'high' }, { priorityKey: 'normal' }, { priorityKey: 'none' }], 1), '<span class="wp-month-dots"><i class="p-critical"></i><i class="p-high"></i><i class="p-normal"></i><em>+2</em></span>', 'month cells cap dots and count the rest like history');
assert(!createForm.includes('wpPriorityColor'), 'the task editor no longer exposes a per-task color picker');
const noColor = { id: 'no-color-save', homeType: 'personal', statusKey: 'open', priorityKey: 'normal', tags: [] };
api.wpApplyTaskForm(noColor, { ...formValues, title: 'Без цвета', kind: 'personal', projectId: '', statusKey: 'open' }, { newTask: true });
assert.strictEqual(noColor.priorityColor, undefined, 'saving the editor does not persist a per-task color');
const startDateMarkup = api.wpTaskCard({ id: 'start-date-task', title: 'Дата старта', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-19', statusKey: 'open', priorityKey: 'normal', tags: [], matrixQuadrant: null }, { compact: true });
assert(startDateMarkup.includes('Начало ·') && !startDateMarkup.includes('План ·'), 'task cards use the renamed start-date label');
const compactDateMarkup = api.wpTaskCard({ id: 'week-date-task', title: 'Короткая карточка', homeType: 'personal', assignee: 'Alex', deadlineAt: '2026-10-19', statusKey: 'open', priorityKey: 'normal', tags: [], matrixQuadrant: null }, { compact: true, inDay: true });
assert(compactDateMarkup.includes('title="До: 19.10.2026"') && compactDateMarkup.includes('До: 19.10</span>'), 'week cells keep the date compact while retaining the full numeric deadline on hover');
const makeTitleClip = (scrollWidth, clientWidth, textContent=longTitle) => {
  const classes = new Set(), properties = {};
  const text = { scrollWidth, clientWidth, textContent };
  const clip = {
    title: '',
    style: { setProperty: (key, value) => { properties[key] = value; }, removeProperty: key => { delete properties[key]; } },
    classList: { add: key => classes.add(key), remove: key => classes.delete(key) },
    querySelector: selector => selector === '.wp-title-marquee-text' ? text : null,
    removeAttribute: key => { if (key === 'title') clip.title = ''; }
  };
  const root = { querySelectorAll: selector => selector === '.wp-title-marquee-clip' ? [clip] : [] };
  return { root, clip, classes, properties };
};
const overflowingTitle = makeTitleClip(322, 122);
api.wpSetupTaskTitleMarquee(overflowingTitle.root);
assert(overflowingTitle.classes.has('is-title-overflowing'));
assert.strictEqual(overflowingTitle.properties['--wp-title-shift'], '-200px');
assert(overflowingTitle.properties['--wp-title-duration'].endsWith('s'));
const fittingTitle = makeTitleClip(120, 122);
api.wpSetupTaskTitleMarquee(fittingTitle.root);
assert(!fittingTitle.classes.has('is-title-overflowing'));
context.__reducedMotion = true;
const reducedTitle = makeTitleClip(322, 122);
api.wpSetupTaskTitleMarquee(reducedTitle.root);
assert(!reducedTitle.classes.has('is-title-overflowing'));
assert.strictEqual(reducedTitle.clip.title, longTitle, 'reduced-motion users can still reveal the full title');
context.__reducedMotion = false;
api.wpSetCalendarMode('month');
assert.strictEqual(api.WP.calendarMode, 'month');
assert.strictEqual(state.ui.plannerCalendarMode, 'month');
api.wpToggleSidebar();
assert.strictEqual(state.ui.plannerSidebarCollapsed, true);
api.wpToggleSidebar();
assert.strictEqual(state.ui.plannerSidebarCollapsed, false);

// Recurrence rules respect the start anchor, interval, weekday selection and month-end clamping.
const weekly = { frequency: 'weekly', interval: 2, weekdays: [1, 3], startDate: '2026-10-05', endDate: null };
assert.strictEqual(api.wpMatchRuleDate(weekly, '2026-10-05'), true);
assert.strictEqual(api.wpMatchRuleDate(weekly, '2026-10-07'), true);
assert.strictEqual(api.wpMatchRuleDate(weekly, '2026-10-12'), false);
assert.strictEqual(api.wpMatchRuleDate(weekly, '2026-10-19'), true);
const monthly = { frequency: 'monthly', interval: 1, dayOfMonth: 31, startDate: '2026-01-31', endDate: null };
assert.strictEqual(api.wpMatchRuleDate(monthly, '2026-02-28'), true);
assert.strictEqual(api.wpMatchRuleDate(monthly, '2026-03-30'), false);
assert.strictEqual(api.wpMatchRuleDate(monthly, '2026-03-31'), true);

// Legacy classification preserves project ownership, explicit Inbox flags, old Eisenhower items and unknown fields.
const legacyBoard = {
  id: 'board-1', name: 'Моя доска', code: 'SHTR-TEST', taskSchemaVersion: 5,
  projects: [{ id: 'project-1', name: 'Проект', members: [] }],
  tasks: [
    { id: 'project-task', title: 'Проектная', kind: 'project', projectId: 'project-1', project: 'Проект', inbox: true, status: 'Готово', prio: 'Выполнено', prevPrio: 'Критично', deadline: '20.10.2026', comments: [{ id: 'legacy-comment-1', projectId: 'project-1', body: 'Сохранённый комментарий', author: 'Alex', createdAt: '2026-10-10T12:00:00.000Z' }] },
    { id: 'inbox-task', title: 'Входящая', inbox: true, status: 'В работе' },
    { id: 'personal-task', title: 'Личная', kind: 'personal', qm: 0, scheduledDate: '2026-10-10', scheduledEndDate: '2026-10-12', customField: 'keep me' },
    { id: 'deadline-wins', title: 'Сохранить заданный срок', kind: 'personal', scheduledDate: '2026-10-10', scheduledEndDate: '2026-10-12', deadlineAt: '2026-10-14' }
  ],
  eisenTasks: [{ id: 'matrix-task', title: 'Из старой матрицы', qm: 1 }], invites: [], ownerEmail: 'owner@example.test'
};
const rawBoard = JSON.stringify(legacyBoard);
const registry = { id: 'board-1', boards: [JSON.parse(rawBoard)], customRegistryField: 'preserve' };
const rawRegistry = JSON.stringify(registry);
data.clear();
data.set('shelter:board', rawBoard);
data.set('shelter:boards', rawRegistry);
data.set('shelter:eisenTasks', JSON.stringify([{ id: 'loose-matrix-task', title: 'Отдельный список', qm: 2 }]));
Object.assign(state, {
  board: JSON.parse(rawBoard), boards: [JSON.parse(rawBoard)], boardsMeta: {}, boardId: 'board-1',
  taskMigrationOK: true, taskMigrationPending: false, taskMigrationError: '',
  taskMigrationPendingKeys: [], taskMigrationBackupKey: '', taskMigrationOriginalRaw: null
});
assert.strictEqual(api.wpMigrateTaskStores(registry), true);
assert.strictEqual(state.taskMigrationPending, true);
assert.strictEqual(state.taskMigrationOK, false, 'old data must remain read-only until user confirms');
assert.strictEqual(data.get('shelter:board'), rawBoard, 'migration preview must not overwrite the source board');
assert.strictEqual(data.get('shelter:boards'), rawRegistry, 'migration preview must not overwrite the source registry');
const backupKey = data.get('shelter:planner:migration-backup:latest');
assert(backupKey, 'a verified migration backup index must be written');
assert(data.get(backupKey).startsWith('ss1:'), 'migration backups are encrypted at rest');
const backup = JSON.parse(api.wpStoreDecrypt(data.get(backupKey)));
assert.strictEqual(backup.kind, 'SHELTER_PLANNER_MIGRATION_BACKUP');
assert.strictEqual(backup.raw['shelter:board'], rawBoard);
assert.strictEqual(backup.raw['shelter:boards'], rawRegistry);
assert.strictEqual(state.boardsMeta.customRegistryField, 'preserve');

const tasks = state.board.tasks;
const projectTask = tasks.find(t => t.id === 'project-task');
const inboxTask = tasks.find(t => t.id === 'inbox-task');
const personalTask = tasks.find(t => t.id === 'personal-task');
assert.strictEqual(api.wpTaskHomeType(projectTask), 'project', 'a project remains the task home if a legacy inbox flag overlaps');
assert.strictEqual(projectTask.legacy.v5InboxFlag, true, 'the overlapping legacy flag is retained for audit');
assert.strictEqual(projectTask.statusKey, 'done');
assert.strictEqual(projectTask.priorityKey, 'critical');
assert.strictEqual(projectTask.comments[0].body, 'Сохранённый комментарий', 'comments survive legacy task migration');
assert(!Object.prototype.hasOwnProperty.call(projectTask.legacy, 'comments'), 'comments are preserved as task data rather than duplicated as legacy metadata');
assert.strictEqual(api.wpTaskHomeType(inboxTask), 'inbox');
assert.strictEqual(api.wpTaskHomeType(personalTask), 'personal', 'legacy quadrant fields never change the task home');
assert.strictEqual(personalTask.legacy.customField, 'keep me', 'unknown legacy fields must survive migration');
assert.strictEqual(personalTask.deadlineAt, '2026-10-12', 'a legacy calendar end migrates into the existing deadline field');
assert(!Object.prototype.hasOwnProperty.call(personalTask, 'scheduledEndDate'), 'the migrated task no longer stores a separate calendar end date');
assert(!Object.prototype.hasOwnProperty.call(personalTask.legacy, 'scheduledEndDate'), 'the legacy end date is normalized, not retained as opaque metadata');
assert.strictEqual(tasks.find(t => t.id === 'deadline-wins').deadlineAt, '2026-10-14', 'an already-set deadline takes precedence over the legacy calendar end');
assert(!Object.prototype.hasOwnProperty.call(tasks.find(t => t.id === 'deadline-wins'), 'scheduledEndDate'));
assert.strictEqual(api.wpTaskHomeType(tasks.find(t => t.id === 'matrix-task')), 'personal', 'imported Eisenhower tasks land in personal tasks');
assert.strictEqual(api.wpTaskHomeType(tasks.find(t => t.id === 'loose-matrix-task')), 'personal');
assert.strictEqual(state.board.taskSchemaVersion, 7);

assert.strictEqual(api.wpFinalizeTaskMigration(), true);
assert.strictEqual(state.taskMigrationOK, true);
assert.strictEqual(state.taskMigrationPending, false);
assert.strictEqual(JSON.parse(api.wpStoreDecrypt(data.get('shelter:board'))).taskSchemaVersion, 7);
assert.strictEqual(JSON.parse(api.wpStoreDecrypt(data.get('shelter:boards'))).customRegistryField, 'preserve');
assert.strictEqual(data.has('shelter:eisenTasks'), false, 'legacy standalone matrix data is removed only after verified v7 persistence');
assert(data.has(backupKey), 'the original migration backup remains available after migration');

// Schema v6 data is migrated with a verified backup, folding task and occurrence calendar ends into deadlines.
const v6Board = {
  id: 'board-v6', name: 'Доска v6', code: 'SHTR-V6', taskSchemaVersion: 6,
  projects: [], people: [], invites: [],
  tasks: [{ id: 'v6-task', schemaVersion: 6, title: 'Старая задача', homeType: 'personal', scheduledDate: '2026-10-10', scheduledEndDate: '2026-10-12', deadlineAt: null, statusKey: 'open', priorityKey: 'normal' }],
  recurrenceRules: [],
  occurrences: [
    { id: 'v6-task::2026-10-10', seriesTaskId: 'v6-task', originalDate: '2026-10-10', scheduledDate: '2026-10-10', scheduledEndDate: '2026-10-12', statusKey: 'open', overrides: {} },
    { id: 'v6-task::2026-10-11', seriesTaskId: 'v6-task', originalDate: '2026-10-11', scheduledDate: '2026-10-11', statusKey: 'open', overrides: { scheduledEndDate: '2026-10-13' } }
  ]
};
const rawV6Board = JSON.stringify(v6Board);
const registryV6 = { id: 'board-v6', boards: [JSON.parse(rawV6Board)] };
const rawRegistryV6 = JSON.stringify(registryV6);
data.clear();
data.set('shelter:board', rawV6Board);
data.set('shelter:boards', rawRegistryV6);
Object.assign(state, {
  board: JSON.parse(rawV6Board), boards: [JSON.parse(rawV6Board)], boardsMeta: {}, boardId: 'board-v6',
  taskMigrationOK: true, taskMigrationPending: false, taskMigrationError: '',
  taskMigrationPendingKeys: [], taskMigrationBackupKey: '', taskMigrationOriginalRaw: null
});
assert.strictEqual(api.wpMigrateTaskStores(registryV6), true);
assert.strictEqual(state.taskMigrationPending, true, 'v6 data awaits explicit confirmation before storage is rewritten');
assert.strictEqual(data.get('shelter:board'), rawV6Board, 'the v6 source board stays unchanged until confirmation');
assert.strictEqual(state.board.taskSchemaVersion, 7);
const migratedV6Task = state.board.tasks.find(task => task.id === 'v6-task');
assert.strictEqual(migratedV6Task.deadlineAt, '2026-10-12');
assert(!Object.prototype.hasOwnProperty.call(migratedV6Task, 'scheduledEndDate'));
const migratedV6Occurrence = state.board.occurrences.find(occurrence => occurrence.originalDate === '2026-10-10');
assert.strictEqual(migratedV6Occurrence.deadlineAt, '2026-10-12');
assert(!Object.prototype.hasOwnProperty.call(migratedV6Occurrence, 'scheduledEndDate'));
const migratedV6Override = state.board.occurrences.find(occurrence => occurrence.originalDate === '2026-10-11');
assert.strictEqual(migratedV6Override.overrides.deadlineAt, '2026-10-13');
assert(!Object.prototype.hasOwnProperty.call(migratedV6Override.overrides, 'scheduledEndDate'));
assert.strictEqual(api.wpFinalizeTaskMigration(), true);
const savedV7Board = JSON.parse(api.wpStoreDecrypt(data.get('shelter:board')));
assert.strictEqual(savedV7Board.taskSchemaVersion, 7);
assert(!Object.prototype.hasOwnProperty.call(savedV7Board.tasks[0], 'scheduledEndDate'));

// A failed backup or malformed source leaves the original strings untouched and blocks writes.
data.clear();
data.set('shelter:board', rawBoard);
data.set('shelter:boards', rawRegistry);
Object.assign(state, {
  board: JSON.parse(rawBoard), boards: [JSON.parse(rawBoard)], boardId: 'board-1', boardsMeta: {},
  taskMigrationOK: true, taskMigrationPending: false, taskMigrationError: '', taskMigrationPendingKeys: []
});
context.__denyBackup = true;
assert.strictEqual(api.wpMigrateTaskStores(registry), false);
assert.strictEqual(state.taskMigrationOK, false);
assert.strictEqual(data.get('shelter:board'), rawBoard);
assert.strictEqual(data.get('shelter:boards'), rawRegistry);
context.__denyBackup = false;

data.clear();
const corrupt = '{broken-json';
data.set('shelter:board', corrupt);
Object.assign(state, { board: null, boards: null, boardId: '', taskMigrationOK: true, taskMigrationPending: false });
assert.strictEqual(api.wpMigrateTaskStores(null), false);
assert.strictEqual(state.taskMigrationOK, false);
assert.strictEqual(data.get('shelter:board'), corrupt);

// Encrypted task storage: ciphertext fixtures migrate transparently, writes stay encrypted, crypto failures never overwrite data.
assert.strictEqual(api.wpStoreParse(null).value, null);
assert.strictEqual(api.wpStoreParse('').value, null);
assert.strictEqual(api.wpStoreParse('{"a":1}').value.a, 1, 'legacy plaintext stores keep parsing');
assert.strictEqual(api.wpStoreParse('ss1:{"a":1}').value.a, 1, 'ss1: blobs decrypt before parsing');
assert.strictEqual(api.wpStoreParse('{broken').ok, false);
assert.strictEqual(api.wpStoreParse('{broken').reason, 'shape');
assert.strictEqual(api.wpStoreParse('ss1:{broken').ok, false);
data.clear();
data.set('shelter:board', api.wpStoreEncrypt(rawBoard));
data.set('shelter:boards', api.wpStoreEncrypt(rawRegistry));
Object.assign(state, {
  board: JSON.parse(rawBoard), boards: [JSON.parse(rawBoard)], boardsMeta: {}, boardId: 'board-1',
  taskMigrationOK: true, taskMigrationPending: false, taskMigrationError: '',
  taskMigrationPendingKeys: [], taskMigrationBackupKey: '', taskMigrationOriginalRaw: null
});
assert.strictEqual(api.wpMigrateTaskStores(registry), true, 'an encrypted store migrates like a plaintext one');
assert.strictEqual(api.wpFinalizeTaskMigration(), true);
assert(data.get('shelter:board').startsWith('ss1:'), 'finalized boards are encrypted at rest');
assert.strictEqual(JSON.parse(api.wpStoreDecrypt(data.get('shelter:board'))).taskSchemaVersion, 7);
context.__failSecret = true;
assert.strictEqual(api.wpStoreEncrypt('{}'), '', 'a crypto failure yields no ciphertext');
data.set('shelter:board', rawBoard);
data.set('shelter:boards', rawRegistry);
Object.assign(state, { board: JSON.parse(rawBoard), boards: [JSON.parse(rawBoard)], boardId: 'board-1', taskMigrationOK: true, taskMigrationPending: false, taskMigrationError: '', taskMigrationPendingKeys: [] });
assert.strictEqual(api.wpMigrateTaskStores(registry), false, 'migration refuses to run when the backup cannot be encrypted');
assert.strictEqual(data.get('shelter:board'), rawBoard);
assert.strictEqual(api.wpPersistBoard(), false, 'commits fail closed when encryption is unavailable');
assert.strictEqual(data.get('shelter:board'), rawBoard);
context.__failSecret = false;

assert.strictEqual(typeof api.render.tasks, 'function');
assert.strictEqual(typeof api.actions.taskEdit, 'function');
assert.strictEqual(typeof api.actions.wpView, 'function');
(async function testPasswordProtectedTaskTransfer() {
  const boardA = {
    id: 'board-a', name: 'Рабочая доска', code: 'BOARD-A', taskSchemaVersion: 7,
    people: ['Алиса', 'Борис', 'Лишний участник'],
    projects: [
      { id: 'project-a', name: 'Проект Альфа', members: [{ name: 'Алиса' }] },
      { id: 'project-b', name: 'Проект Бета', members: [{ name: 'Борис' }] }
    ],
    tasks: [
      { id: 'task-a', title: 'ALPHA SECRET TASK', homeType: 'project', homeId: 'project-a', projectId: 'project-a', project: 'Проект Альфа', assignee: 'Алиса', recurrenceRuleId: 'rule-a' },
      { id: 'task-b', title: 'BETA TASK', homeType: 'project', homeId: 'project-b', projectId: 'project-b', project: 'Проект Бета', assignee: 'Борис', recurrenceRuleId: 'rule-b' },
      { id: 'task-personal', title: 'PERSONAL TASK', homeType: 'personal' }
    ],
    recurrenceRules: [{ id: 'rule-a', taskId: 'task-a' }, { id: 'rule-b', taskId: 'task-b' }],
    occurrences: [{ id: 'occ-a', seriesTaskId: 'task-a' }, { id: 'occ-b', seriesTaskId: 'task-b' }],
    invites: [{ id: 'invite-a', projectId: 'project-a' }, { id: 'invite-b', projectId: 'project-b' }],
    ownerEmail: 'owner@example.test'
  };
  const boardB = {
    id: 'board-b', name: 'Домашняя доска', code: 'BOARD-B', taskSchemaVersion: 7,
    people: [], projects: [{ id: 'project-c', name: 'Проект Гамма', members: [] }],
    tasks: [{ id: 'task-c', title: 'GAMMA TASK', homeType: 'project', homeId: 'project-c', projectId: 'project-c', project: 'Проект Гамма' }],
    recurrenceRules: [], occurrences: [], invites: []
  };
  Object.assign(state, { board: boardA, boards: [boardA, boardB], boardId: 'board-a', boardsMeta: { secret: 'metadata' }, taskMigrationOK: true });

  const allPayload = api.wpExportPayload('all');
  assert.strictEqual(allPayload.exportScope.type, 'all');
  assert.strictEqual(allPayload.boards.length, 2, 'the all-data option includes every local board');
  assert(allPayload.boards.some(board => board.id === 'board-b' && board.tasks.some(task => task.id === 'task-c')));

  const selection = [{ boardId: 'board-a', projectId: 'project-a' }];
  const selectedPayload = api.wpExportPayload('projects', selection);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(selectedPayload.exportScope.projects)), selection);
  assert.strictEqual(selectedPayload.boards.length, 1);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(selectedPayload.boards[0].projects.map(project => project.id))), ['project-a']);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(selectedPayload.boards[0].tasks.map(task => task.id))), ['task-a']);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(selectedPayload.boards[0].recurrenceRules.map(rule => rule.id))), ['rule-a']);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(selectedPayload.boards[0].occurrences.map(record => record.id))), ['occ-a']);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(selectedPayload.boards[0].invites.map(invite => invite.id))), ['invite-a']);
  assert(!JSON.stringify(selectedPayload).includes('BETA TASK') && !JSON.stringify(selectedPayload).includes('Лишний участник'));
  assert.strictEqual(state.boards[0].projects.length, 2, 'building a project export does not mutate local data');
  const selfId = api.wpSelfTaskId();
  assert(/^SH-[A-Z2-9]{4}-[A-Z2-9]{4}$/.test(selfId), 'the local invite id has a readable SH-XXXX-XXXX shape');
  assert.strictEqual(api.wpSelfTaskId(), selfId, 'the local invite id stays stable across calls');
  assert.strictEqual(api.wpSaveTaskContact('sh-aaaa-bbbb', 'Иван'), true);
  assert.strictEqual(api.wpSaveTaskContact('', 'Без ID'), false, 'contacts require both an id and a nick');
  assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpTaskContacts())), [{ id: 'SH-AAAA-BBBB', nick: 'Иван' }], 'contacts are stored with a normalized id');
  assert(api.wpTaskContactOptionsHtml('SH-AAAA-BBBB').includes('Иван · SH-AAAA-BBBB'));
  assert.strictEqual(api.wpPersonName('Иван'), 'Иван');
  assert.strictEqual(api.wpPersonName({ name: 'Иван', contactId: 'SH-AAAA-BBBB' }), 'Иван');
  assert.strictEqual(api.wpPersonContactId({ name: 'Иван', contactId: 'sh-aaaa-bbbb' }), 'SH-AAAA-BBBB');
  assert.strictEqual(api.wpPersonContactId('Иван'), '');
  const personBoard = { people: ['Иван'] };
  assert.strictEqual(api.wpBoardAddPerson(personBoard, 'Анна', 'sh-cccc-dddd'), true);
  assert.deepStrictEqual(JSON.parse(JSON.stringify(personBoard.people)), ['Иван', { name: 'Анна', contactId: 'SH-CCCC-DDDD' }], 'assignees added with an invite id keep it');
  assert.strictEqual(api.wpBoardAddPerson(personBoard, 'иван', 'SH-AAAA-BBBB'), true, 'an invite id links to the existing namesake');
  assert.deepStrictEqual(JSON.parse(JSON.stringify(personBoard.people[0])), { name: 'Иван', contactId: 'SH-AAAA-BBBB' });
  assert.strictEqual(api.wpBoardAddPerson(personBoard, 'Иван', 'SH-AAAA-BBBB'), false, 're-adding the same assignee is a no-op');
  assert.strictEqual(api.wpBoardPersonIndex(personBoard, 'АННА'), 1, 'assignee lookup ignores case');
  assert(html.includes('id="dAddId"'), 'the board settings assignee tab has an invite-id field');
  assert(html.includes('function personDlg('), 'editing an assignee uses a single name+id dialog');
  assert(html.includes('data-pp-name') && html.includes('data-pp-id'), 'the assignee dialog has both fields');
  assert(html.includes("personDlg({ title: 'Исполнитель'"), 'the board settings edit flow opens the assignee dialog');
assert(html.includes("const D = [['projects', 'Проекты'], ['people', 'Исполнители'], ['prios', 'Приоритеты'], ['statuses', 'Статусы']];"), 'board settings offer no unused terms/formats dictionaries');
  assert(html.includes('Исполнитель с таким именем уже есть'), 'duplicate assignee names trigger a warning');
  assert(html.includes('Этот ID уже привязан к исполнителю'), 'duplicate invite ids trigger a warning');
  assert(html.includes('wpBoardAddPerson(board,nm,cid)'), 'export contacts join the board assignee list');
  assert(html.includes('wpBoardPersonIndexById(board,cid)'), 'export resolves contacts against board assignees by id');
  assert.strictEqual(api.wpBoardPersonIndexById(personBoard, 'sh-cccc-dddd'), 1, 'assignee lookup by id ignores case');
  assert.strictEqual(api.wpBoardPersonIndexById(personBoard, 'SH-QQQQ-QQQQ'), -1);
  assert.strictEqual(api.wpSaveTaskContact('SH-CCCC-DDDD', 'Анна'), true);
  assert.strictEqual(api.wpDeleteTaskContact('sh-cccc-dddd'), true, 'replaced invite ids drop their stale contact');
  assert.deepStrictEqual(JSON.parse(JSON.stringify(api.wpTaskContacts())), [{ id: 'SH-AAAA-BBBB', nick: 'Иван' }]);
  assert.strictEqual(api.wpDeleteTaskContact('SH-QQQQ-QQQQ'), false);
  const prevBoard = state.board, prevBoards = state.boards;
  state.board = { id: 'person-board', people: ['Иван', { name: 'Анна', contactId: 'SH-CCCC-DDDD' }] };
  state.boards = [];
  assert.strictEqual(api.wpContactIdUsedAnywhere('sh-cccc-dddd'), true);
  assert.strictEqual(api.wpContactIdUsedAnywhere('SH-QQQQ-QQQQ'), false);
  state.board = prevBoard; state.boards = prevBoards;
  const executorPayload = api.wpExportPayload('all', [], { target: 'executor', targetId: 'sh-aaaa-bbbb', targetNick: 'Иван' });
  assert.strictEqual(executorPayload.transfer.target, 'executor');
  assert.strictEqual(executorPayload.transfer.targetId, 'SH-AAAA-BBBB');
  assert.strictEqual(executorPayload.transfer.targetNick, 'Иван');
  assert.strictEqual(executorPayload.transfer.fromId, selfId, 'exports carry the sender invite id');
  assert(executorPayload.boards.every(board => board.tasks.every(task => task.transferToId === 'SH-AAAA-BBBB')), 'executor exports stamp their task copies');
  assert(!JSON.stringify(state.boards).includes('transferToId'), 'stamping never touches local tasks');
  const selfPayload = api.wpExportPayload('all');
  assert.strictEqual(selfPayload.transfer.target, 'self');
  const stampedBoards = [{ id: 'b1', tasks: [{ id: 't1', title: 'Для меня', assignee: 'Кто-то' , transferToId: 'SH-AAAA-BBBB' }] }];
  assert.strictEqual(api.wpApplyTransferRemap(stampedBoards, executorPayload.transfer), 1);
  assert.strictEqual(stampedBoards[0].tasks[0].assignee, 'me', 'importing an executor file makes the importer the assignee');
  assert.strictEqual(api.wpAssigneeLabel(stampedBoards[0].tasks[0]), 'Alex');
  assert(!('transferToId' in stampedBoards[0].tasks[0]), 'transfer stamps are stripped on import');
  const legacyStamped = [{ id: 'b2', tasks: [{ id: 't2', title: 'Старый штамп', assignee: 'Анна', transferToId: 'SH-AAAA-BBBB' }] }];
  assert.strictEqual(api.wpApplyTransferRemap(legacyStamped, selfPayload.transfer), 0, 'self imports keep assignees');
  assert.strictEqual(legacyStamped[0].tasks[0].assignee, 'Анна');

  const envelope = await api.wpEncryptTaskPayload(selectedPayload, 'Correct horse battery');
  const fileText = JSON.stringify(envelope);
  assert.strictEqual(envelope.format, 'SHELTER_TASK_EXPORT');
  assert.strictEqual(envelope.cipher, 'AES-256-GCM');
  assert.strictEqual(envelope.kdf, 'PBKDF2-SHA-256');
  assert.strictEqual(envelope.iterations, 600000);
  assert(!fileText.includes('ALPHA SECRET TASK') && !fileText.includes('owner@example.test'), 'the downloaded JSON envelope contains ciphertext, not task contents');
  const decrypted = await api.wpDecryptTaskPayload(envelope, 'Correct horse battery');
  assert.strictEqual(JSON.stringify(decrypted), JSON.stringify(selectedPayload), 'a correct code restores the exact protected payload');
  assert(api.wpReadTaskExportBoards(decrypted), 'only a valid decrypted SHELTER task payload is importable');

  let wrongCodeRejected = false;
  try { await api.wpDecryptTaskPayload(envelope, 'Incorrect horse battery'); } catch (_) { wrongCodeRejected = true; }
  assert(wrongCodeRejected, 'a wrong file code fails AES-GCM authentication');
  const tampered = JSON.parse(JSON.stringify(envelope));
  tampered.ciphertext = (tampered.ciphertext[0] === 'A' ? 'B' : 'A') + tampered.ciphertext.slice(1);
  let tamperingRejected = false;
  try { await api.wpDecryptTaskPayload(tampered, 'Correct horse battery'); } catch (_) { tamperingRejected = true; }
  assert(tamperingRejected, 'modified ciphertext is rejected');
  let shortCodeRejected = false;
  try { await api.wpEncryptTaskPayload(selectedPayload, 'short'); } catch (_) { shortCodeRejected = true; }
  assert(shortCodeRejected, 'export requires a nontrivial password');
  let longCodeRejected = false;
  try { await api.wpEncryptTaskPayload(selectedPayload, 'x'.repeat(1025)); } catch (_) { longCodeRejected = true; }
  assert(longCodeRejected, 'password length is capped before key derivation');
  assert.strictEqual(api.wpReadTaskExportBoards({ app: 'SHELTER', boards: selectedPayload.boards }), null, 'unprotected or incomplete task JSON is not accepted by the task importer');
  const migrationRaw = {
    kind: 'SHELTER_PLANNER_MIGRATION_BACKUP', schemaVersion: 7,
    raw: {
      'shelter:board': JSON.stringify(boardA),
      'shelter:boards': JSON.stringify({ id: 'board-a', boards: [boardA] }),
      'shelter:eisenTasks': JSON.stringify([{ id: 'loose-eisen', title: 'Loose legacy task' }])
    }
  };
  const migrationEnvelope = await api.wpEncryptTaskPayload({ kind: 'SHELTER_PLANNER_MIGRATION_BACKUP_FILE', migrationBackupText: JSON.stringify(migrationRaw) }, 'Migration backup code');
  const decryptedMigration = await api.wpDecryptTaskPayload(migrationEnvelope, 'Migration backup code');
  const migrationPayload = api.wpTaskPayloadFromMigrationBackupFile(decryptedMigration);
  assert(migrationPayload && api.wpReadTaskExportBoards(migrationPayload), 'a correctly decrypted technical migration backup is converted to a validated task payload');
  assert(api.wpReadTaskExportBoards(migrationPayload).some(board => board.tasks.some(task => task.id === 'loose-eisen')), 'loose legacy tasks are preserved when importing a migration snapshot');

  const existing = {
    id: 'board-a', name: 'Рабочая доска', code: 'BOARD-A', taskSchemaVersion: 7, people: ['Борис'],
    projects: [{ id: 'project-a', name: 'Старое имя Альфа', members: [] }, { id: 'project-b', name: 'Проект Бета', members: [] }],
    tasks: [
      { id: 'task-a', title: 'OLD ALPHA TASK', homeType: 'project', homeId: 'project-a', projectId: 'project-a', project: 'Старое имя Альфа', recurrenceRuleId: 'old-rule' },
      { id: 'task-b', title: 'BETA TASK', homeType: 'project', homeId: 'project-b', projectId: 'project-b', project: 'Проект Бета' },
      { id: 'personal-existing', title: 'KEEP PERSONAL', homeType: 'personal' }
    ],
    recurrenceRules: [{ id: 'old-rule', taskId: 'task-a' }, { id: 'rule-b', taskId: 'task-b' }],
    occurrences: [{ id: 'old-occ', seriesTaskId: 'task-a' }, { id: 'occ-b', seriesTaskId: 'task-b' }],
    invites: [], customField: 'preserve'
  };
  const otherBoard = { id: 'other-board', name: 'Other', projects: [], tasks: [], recurrenceRules: [], occurrences: [], invites: [] };
  const merge = api.wpMergeImportedProjects([existing, otherBoard], api.wpReadTaskExportBoards(decrypted), selection);
  assert.strictEqual(merge.ok, true);
  const mergedBoard = merge.boards.find(board => board.id === 'board-a');
  assert.deepStrictEqual([...mergedBoard.tasks.map(task => task.id)].sort(), ['personal-existing', 'task-a', 'task-b']);
  assert.deepStrictEqual([...mergedBoard.projects.map(project => project.id)].sort(), ['project-a', 'project-b']);
  assert.deepStrictEqual([...mergedBoard.recurrenceRules.map(rule => rule.id)].sort(), ['rule-a', 'rule-b']);
  assert.deepStrictEqual([...mergedBoard.occurrences.map(record => record.id)].sort(), ['occ-a', 'occ-b']);
  assert.strictEqual(mergedBoard.customField, 'preserve');
  assert(merge.boards.some(board => board.id === 'other-board'), 'project import preserves other boards');
  assert(existing.tasks.some(task => task.title === 'OLD ALPHA TASK'), 'preparing a project import does not mutate the current board');

  const collisionBoard = JSON.parse(JSON.stringify(api.wpReadTaskExportBoards(decrypted)[0]));
  collisionBoard.tasks[0].id = 'task-b';
  const collision = api.wpMergeImportedProjects([existing], [collisionBoard], selection);
  assert.strictEqual(collision.ok, false, 'project import refuses to overwrite an unrelated task with a colliding ID');

  const transferLocal=JSON.parse(JSON.stringify(existing)),transferOther=JSON.parse(JSON.stringify(otherBoard));
  const storedLocalRaw=JSON.stringify(transferLocal),storedRegistryRaw=JSON.stringify({id:'board-a',boards:[transferLocal,transferOther],retainedRegistryValue:true});
  state.board=transferLocal;state.boards=[transferLocal,transferOther];state.boardId='board-a';state.boardsMeta={retainedMetaValue:true};state.taskMigrationOK=true;state.taskMigrationPendingKeys=[];
  data.set('shelter:board',storedLocalRaw);data.set('shelter:boards',storedRegistryRaw);
  for(const key of [...data.keys()]) if(String(key).startsWith('shelter:planner:preimport-backup')) data.delete(key);
  const importedBoard={
    id:'board-a',name:'Чужое имя доски',code:'REMOTE-A',taskSchemaVersion:7,people:['Анна','Борис'],
    projects:[{id:'project-a',name:'Импортированное имя Альфа',members:[]},{id:'project-c',name:'Проект Гамма',members:[]}],
    tasks:[
      {id:'task-a',title:'REMOTE DUPLICATE MUST NOT REPLACE LOCAL',homeType:'project',homeId:'project-a',projectId:'project-a',project:'Импортированное имя Альфа',recurrenceRuleId:'old-rule'},
      {id:'task-c',title:'REMOTE NEW TASK',homeType:'project',homeId:'project-c',projectId:'project-c',project:'Проект Гамма',recurrenceRuleId:'new-rule'}
    ],
    recurrenceRules:[
      {id:'old-rule',taskId:'task-a',frequency:'weekly',startDate:'2026-10-12',weekdays:[1],active:true},
      {id:'new-rule',taskId:'task-c',frequency:'weekly',startDate:'2026-10-12',weekdays:[1],active:true}
    ],
    occurrences:[
      {id:'old-occ',seriesTaskId:'task-a',originalDate:'2026-10-12',scheduledDate:'2026-10-12',statusKey:'done'},
      {id:'new-occ',seriesTaskId:'task-c',originalDate:'2026-10-19',scheduledDate:'2026-10-19',statusKey:'done'}
    ],invites:[{id:'invite-new',projectId:'project-c',email:'anna@example.test'}],customField:'REMOTE MUST NOT REPLACE',remoteOnlyField:'kept'
  };
  const foreignBoard={id:'foreign-board',name:'Доска из другого устройства',code:'FOREIGN',taskSchemaVersion:7,projects:[],people:[],tasks:[{id:'foreign-task',title:'ВИДИМАЯ ИМПОРТИРОВАННАЯ ЗАДАЧА',homeType:'personal',statusKey:'open'}],invites:[],recurrenceRules:[],occurrences:[]};
  const fullPayload={app:'SHELTER',exportSchemaVersion:7,taskSchemaVersion:7,exportScope:{type:'all'},activeBoardId:'foreign-board',board:foreignBoard,boards:[importedBoard,foreignBoard],boardsMeta:{remoteOnlyMeta:true}};
  const fullEnvelope=await api.wpEncryptTaskPayload(fullPayload,'Transfer code on two devices');
  assert(!JSON.stringify(fullEnvelope).includes('REMOTE NEW TASK'),'the transfer file stores encrypted payload rather than plaintext tasks');
  const fullDecrypted=await api.wpDecryptTaskPayload(fullEnvelope,'Transfer code on two devices');
  const fullSourceBoards=api.wpReadTaskExportBoards(fullDecrypted);
  assert(fullSourceBoards&&fullSourceBoards.length===2,'a decrypted full export passes payload validation before import');
  const applied=await api.wpApplyImportedTaskData(fullDecrypted,fullSourceBoards,'all',[]);
  assert.strictEqual(applied,true,'a correctly decrypted full export is applied after confirmation and backup');
  assert.strictEqual(state.boardId,'foreign-board','a newly imported board becomes active so its tasks are visible on another device');
  assert(state.board.tasks.some(task=>task.id==='foreign-task'),'the imported board task is present in the active board');
  const transferredBoard=state.boards.find(board=>board.id==='board-a');
  assert.deepStrictEqual([...transferredBoard.tasks.map(task=>task.id)].sort(),['personal-existing','task-a','task-b','task-c']);
  assert.strictEqual(transferredBoard.tasks.find(task=>task.id==='task-a').title,'OLD ALPHA TASK','a matching task ID keeps the local task instead of overwriting it');
  assert.deepStrictEqual([...transferredBoard.projects.map(project=>project.id)].sort(),['project-a','project-b','project-c']);
  assert.deepStrictEqual([...transferredBoard.recurrenceRules.map(rule=>rule.id)].sort(),['new-rule','old-rule','rule-b']);
  assert.deepStrictEqual([...transferredBoard.occurrences.map(record=>record.id)].sort(),['new-occ','occ-b','old-occ']);
  assert.strictEqual(transferredBoard.customField,'preserve','local board fields survive the full import');
  assert.strictEqual(transferredBoard.remoteOnlyField,'kept','missing board metadata is copied from the export');
  assert(transferredBoard.invites.some(invite=>invite.id==='invite-new'));
  assert(state.boards.some(board=>board.id==='other-board'),'unrelated local boards survive the full import');
  assert.strictEqual(state.boardsMeta.retainedMetaValue,true);assert.strictEqual(state.boardsMeta.remoteOnlyMeta,true);
  const preImportKey=data.get('shelter:planner:preimport-backup:latest');
  const preImport=JSON.parse(data.get(preImportKey));
  assert.strictEqual(preImport.kind,'SHELTER_PLANNER_PRE_IMPORT_BACKUP');
  assert.strictEqual(preImport.raw['shelter:board'],storedLocalRaw,'the exact original local board is backed up before import');
  assert.strictEqual(preImport.raw['shelter:boards'],storedRegistryRaw,'the exact original local registry is backed up before import');
  const repeated=api.wpMergeImportedTaskData(state.boards,fullSourceBoards,state.boardId,fullDecrypted.activeBoardId);
  assert.strictEqual(repeated.ok,true);assert.strictEqual(repeated.boards.find(board=>board.id==='board-a').tasks.length,4,'re-importing the same full file does not duplicate task IDs');

  for (const key of [...data.keys()]) if (String(key).startsWith('shelter:planner:migration-backup')) data.delete(key);
  api.wpDownloadMigrationBackup();
  assert(context.__lastModalHtml.includes('текущий снимок данных задач'), 'the migration-copy action offers a real current-data snapshot when no historical migration copy exists');
  assert.strictEqual(typeof api.actions.taskExport, 'function');
  // The month summary leads with the active-task count.
  const monthSummary = api.wpMonthSummary([{ statusKey: 'open' }, { statusKey: 'waiting' }, { statusKey: 'done' }, { statusKey: 'cancelled' }]);
  assert(monthSummary.includes('<b>2</b> активных'), 'the month summary counts active tasks');
  assert(monthSummary.indexOf('активных') < monthSummary.indexOf('задач'), 'active tasks lead the month summary');
  console.log('UI task planner, guarded migration, and password-protected import/export tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
