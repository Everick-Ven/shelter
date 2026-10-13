'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');
const marker = '/* =========================================================\n   SHELTER · Планировщик задач (v6 · списки и проекты)';
const start = html.indexOf(marker);
assert(start >= 0, 'the v6 planner module must be installed in the active UI');
const end = html.indexOf('\ninit();\n})();', start);
assert(end > start, 'the planner module must load before the application initializes');
const planner = html.slice(start, end);

assert(html.includes('<style id="shelter-week-planner-style">'), 'the task planner stylesheet must be present');
assert(html.includes('wpMigrateTaskStores(breg)'), 'legacy task migration must run during load');
assert(planner.includes('function wpFinalizeTaskMigration()'), 'migration must require an explicit confirmation step');
assert(planner.includes('debounce(e=>{WP.query=e.target.value;wpRenderView();},150)'), 'task search must be debounced by 150 ms');
assert(planner.includes("kb('⌘⇧K')"), 'the task search shortcut label must adapt to the platform');

const harness = String.raw`
const DAY = 86400000;
const __data = globalThis.__data;
const __state = globalThis.__state;
let __uid = 0;
const store = {
  get(k) { return __data.has(k) ? __data.get(k) : null; },
  set(k, v) {
    if (globalThis.__denyBackup && String(k).startsWith('shelter:planner:migration-backup')) return;
    __data.set(k, String(v));
  },
  del(k) { __data.delete(k); },
  json(k, d) { const v = this.get(k); if (v == null || v === '') return d; try { return JSON.parse(v); } catch (_) { return d; } },
  put(k, v) { this.set(k, JSON.stringify(v)); }
};
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
const accountName = () => (S.account && S.account.name) || 'Вы';
const defaultBoard = () => ({ id: '', name: 'Моя доска', code: '', projects: [], people: ['Вы'], prios: [], terms: [], formats: [], statuses: [], tasks: [], invites: [], ownerEmail: '' });
const ensureBoard = () => { if (!S.board) S.board = defaultBoard(); if (!Array.isArray(S.board.tasks)) S.board.tasks = []; if (!Array.isArray(S.board.projects)) S.board.projects = []; };
const canSeeTask = () => true;
const visibleProjects = () => (S.board && S.board.projects) || [];
const addProject = name => { const p = { id: uid('prj'), name, members: [] }; S.board.projects.push(p); return p; };
const plural = (n, forms) => { const a = Math.abs(n) % 100, b = a % 10; return a > 10 && a < 20 ? forms[2] : b > 1 && b < 5 ? forms[1] : b === 1 ? forms[0] : forms[2]; };
const fmtDur = n => String(n);
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const ico = () => '';
const toast = () => {};
const downloadFile = () => {};
const openMenu = () => {};
const openModal = () => ({ scrim: { addEventListener() {}, querySelector() { return null; } }, guard: null });
const closeModal = () => {};
const confirmDlg = async () => true;
const promptDlg = async () => 'test';
const taskDicts = () => {};
const projectInvite = () => {};
const saveNow = () => {};
const persist = () => {};
const refreshPage = () => {};
const renderBadges = () => {};
const curTab = () => null;
const urlMeta = () => ({ page: 'tasks' });
const storeUnused = null;
`;

const state = {
  board: null, boards: null, boardsMeta: {}, boardId: '', ui: {}, account: null,
  taskMigrationOK: true, taskMigrationPending: false, taskMigrationError: '',
  taskMigrationPendingKeys: [], taskMigrationBackupKey: '', taskMigrationOriginalRaw: null,
  taskMigrationPreviewShown: false
};
const data = new Map();
const context = { __state: state, __data: data, __denyBackup: false, __window: {}, __reducedMotion: false, console, setTimeout, clearTimeout };
vm.runInNewContext(harness + '\n' + planner + `
globalThis.__api = {
  schema: WP_SCHEMA, WP, S, store, actions: ACT, render: RENDER, after: AFTER,
  wpDateKey, wpWeekStart, wpAddDays, wpMonthStart, wpMonthKeys, wpFmtMonth, wpMatchRuleDate,
  wpTaskHomeType, wpIsMine, wpWeekEntities, wpMonthEntities, wpRangeEntities, wpCreateTask, wpApplyTaskForm,
  wpTaskCoversDate, wpTaskHasDateSpan, wpCalendarSpanLayout, wpEffectiveOccurrence, wpApplySchedule,
  wpDeadlineText, wpTaskCard, wpSetupTaskTitleMarquee, wpEditorBaseValues, wpEditorBodyHtml, wpNavHtml, wpViewHtml,
  wpSetCalendarMode, wpCalendarMove, wpToggleSidebar,
  wpNormalizeBoard, wpMigrateTaskStores, wpFinalizeTaskMigration
};`, context, { timeout: 5000 });
const api = context.__api;

// Date arithmetic stays local-date based and rejects impossible calendar dates.
assert.strictEqual(api.wpDateKey('13.10.2026'), '2026-10-13');
assert.strictEqual(api.wpDateKey('2026-02-30'), '');
assert.strictEqual(api.wpWeekStart('2026-10-13'), '2026-10-12');
assert.strictEqual(api.wpAddDays('2026-12-31', 1), '2027-01-01');
assert.strictEqual(api.wpMonthStart('2026-10-13'), '2026-10-01');
assert.strictEqual(api.wpFmtMonth('2026-10-13'), 'Октябрь 2026');
const octoberGrid = Array.from(api.wpMonthKeys('2026-10-01'));
assert.strictEqual(octoberGrid.length, 35);
assert.strictEqual(octoberGrid[0], '2026-09-28');
assert.strictEqual(octoberGrid.at(-1), '2026-11-01');

// Calendar views include only the signed-in person's tasks, while accepting the Russian self-assignee aliases.
state.account = { name: 'Alex', email: 'alex@example.test' };
state.board = {
  id: 'calendar-board', ownerEmail: 'alex@example.test', projects: [{ id: 'project-1', name: 'Проект', members: [] }],
  people: [], invites: [], occurrences: [],
  recurrenceRules: [
    { id: 'weekly-self', taskId: 'recurring-self', frequency: 'weekly', interval: 1, weekdays: [1], startDate: '2026-10-05', endDate: null, active: true },
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
    { id: 'recurring-self', title: 'Повтор личной задачи', homeType: 'personal', assignee: 'я', scheduledDate: '2026-10-05', scheduledEndDate: '2026-10-06', recurrenceRuleId: 'weekly-self', statusKey: 'open', priorityKey: 'normal', order: 9 },
    { id: 'recurring-other', title: 'Повтор чужой задачи', homeType: 'personal', assignee: 'Другой исполнитель', scheduledDate: '2026-10-05', recurrenceRuleId: 'weekly-other', statusKey: 'open', priorityKey: 'normal', order: 10 }
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
const weekIds = new Set(Array.from(api.wpWeekEntities('2026-10-12'), task => task.id));
assert.deepStrictEqual([...weekIds].sort(), ['recurring-self::2026-10-12', 'self-email', 'self-legacy-matrix', 'self-name', 'self-ya', 'self-you'].sort());
const monthIds = new Set(Array.from(api.wpMonthEntities('2026-10-01'), task => task.id));
assert(monthIds.has('self-next-week'), 'month view must include all of the current month, beyond the selected week');
assert(monthIds.has('recurring-self::2026-10-26'), 'month view must materialize weekly recurring tasks');
assert(!monthIds.has('other-person') && !monthIds.has('recurring-other::2026-10-12'), 'neither one-off nor recurring tasks for other assignees belong in the personal calendar');
assert(!monthIds.has('unassigned'), 'the calendar must not treat an unassigned task as the signed-in user’s task');
const recurrenceOverlapIds = new Set(Array.from(api.wpRangeEntities('2026-10-06', '2026-10-12'), task => task.id));
assert(recurrenceOverlapIds.has('recurring-self::2026-10-05'), 'a repeating multi-day task remains visible when its start is before the requested range');
const recurringSpan = state.board.tasks.find(task => task.id === 'recurring-self');
assert.strictEqual(api.wpEffectiveOccurrence(recurringSpan, '2026-10-19', null).scheduledEndDate, '2026-10-20', 'recurrence instances inherit the series span');
assert.strictEqual(api.wpEffectiveOccurrence(recurringSpan, '2026-10-19', { id: 'recurring-self::2026-10-19', scheduledDate: '2026-10-19', scheduledEndDate: null }).scheduledEndDate, null, 'an occurrence can explicitly clear its inherited end date');
api.WP.calendarMode = 'month';
const monthMarkup = api.wpViewHtml();
assert(monthMarkup.includes('wp-month-grid') && monthMarkup.includes('Октябрь 2026'));
assert(monthMarkup.includes('Исполнитель Я') && !monthMarkup.includes('Чужая задача'));
assert(!api.wpNavHtml().includes('data-view="matrix"') && !api.wpNavHtml().includes('label:"Матрица"'), 'Matrix must no longer be a planner navigation destination');
const rangeTasks = [
  { id: 'span-before-month', title: 'Срок на границе месяца', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-09-25', scheduledEndDate: '2026-10-02', deadlineAt: '2026-10-03', statusKey: 'open', priorityKey: 'normal', matrixQuadrant: 1, order: 20, tags: [] },
  { id: 'week-span', title: 'Задача на несколько дней', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-10', scheduledEndDate: '2026-10-13', statusKey: 'open', priorityKey: 'normal', matrixQuadrant: 0, order: 21, tags: [] },
  { id: 'month-span-a', title: 'Подготовка A', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-14', scheduledEndDate: '2026-10-16', statusKey: 'open', priorityKey: 'normal', matrixQuadrant: 1, order: 22, tags: [] },
  { id: 'month-span-b', title: 'Подготовка B', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-14', scheduledEndDate: '2026-10-17', statusKey: 'open', priorityKey: 'normal', matrixQuadrant: 2, order: 23, tags: [] },
  { id: 'single-day-task', title: 'Однодневная задача', homeType: 'personal', assignee: 'Alex', scheduledDate: '2026-10-15', scheduledEndDate: null, statusKey: 'open', priorityKey: 'normal', matrixQuadrant: null, order: 24, tags: [] }
];
state.board.tasks.push(...rangeTasks);
assert(api.wpTaskCoversDate(rangeTasks[0], '2026-10-01'));
assert(!api.wpTaskCoversDate(rangeTasks[0], '2026-10-03'));
assert(api.wpTaskHasDateSpan(rangeTasks[0]));
const monthRangeIds = new Set(Array.from(api.wpMonthEntities('2026-10-01'), task => task.id));
assert(monthRangeIds.has('span-before-month'), 'month view must include ranges that begin before its visible grid');
api.WP.monthStart = '2026-10-01'; api.WP.mobileDate = '2026-10-01'; api.WP.calendarMode = 'month';
const rangeMonthMarkup = api.wpViewHtml();
assert(rangeMonthMarkup.includes('wp-month-span-layer') && rangeMonthMarkup.includes('wp-calendar-span--month'));
assert(rangeMonthMarkup.includes('Задача на несколько дней'), 'ranges scheduled in October are rendered as continuous month bars');
assert(rangeMonthMarkup.includes('Срок на границе месяца') && rangeMonthMarkup.includes('25 сент. 2026 — 2 окт. 2026'), 'month bars show a task that crosses into October and retain its full range');
assert(rangeMonthMarkup.includes('wp-month-selected') && rangeMonthMarkup.includes('Срок на границе месяца'), 'a range task is listed when the selected date is inside its span');
const weekDays = Array.from({ length: 7 }, (_, i) => api.wpAddDays('2026-10-12', i));
const weekSpanEntities = api.wpWeekEntities('2026-10-12');
const weekLayout = api.wpCalendarSpanLayout(weekDays, weekSpanEntities);
assert.strictEqual(weekLayout.laneCount, 2, 'overlapping all-day ranges occupy separate lanes');
assert(weekLayout.segments.find(segment => segment.task.id === 'week-span').continuesBefore, 'week range bars clip at the week boundary');
api.WP.weekStart = '2026-10-12'; api.WP.mobileDate = '2026-10-14'; api.WP.calendarMode = 'week';
const rangeWeekMarkup = api.wpViewHtml();
assert(rangeWeekMarkup.includes('wp-week-grid is-all-day') && rangeWeekMarkup.includes('wp-week-span-layer'));
assert(rangeWeekMarkup.includes('Однодневная задача') && rangeWeekMarkup.includes('Задача на несколько дней'));
assert(rangeWeekMarkup.includes('Многодневные задачи на весь день'), 'date-only tasks remain in all-day lanes without invented time slots');
assert.strictEqual(api.wpApplySchedule({ id: 'week-span', entityType: 'task' }, '2026-10-12'), true);
assert.strictEqual(state.board.tasks.find(task => task.id === 'week-span').scheduledDate, '2026-10-12');
assert.strictEqual(state.board.tasks.find(task => task.id === 'week-span').scheduledEndDate, '2026-10-15', 'moving a task preserves its calendar span');
assert.strictEqual(api.wpApplySchedule({ id: 'week-span', entityType: 'task' }, null), true);
assert.strictEqual(state.board.tasks.find(task => task.id === 'week-span').scheduledEndDate, null, 'clearing a plan clears its separate end date');

// Classification stays independent of a task's normal destination; creation has no status control, but editing does.
state.board.tasks = [];
const createdPersonal = api.wpCreateTask('Личная с меткой', '2026-10-13', null, 2, 'Я', false, 'personal');
assert(createdPersonal);
assert.strictEqual(createdPersonal.homeType, 'personal');
assert.strictEqual(createdPersonal.matrixQuadrant, 2);
assert.strictEqual(createdPersonal.statusKey, 'open');
const formValues = {
  title: 'Новая задача', kind: 'personal', projectId: '', assignee: 'Я', scheduledDate: '2026-10-13', scheduledEndDate: '2026-10-15', deadlineAt: '',
  statusKey: 'open', priorityKey: 'normal', quadrant: '1', description: '', tags: '', durationEstimateMinutes: '',
  waitingReason: '', reviewAt: '', frequency: 'none', interval: 1, intervalDays: 1, weekdays: [1], mode: 'calendar', endDate: ''
};
const createForm = api.wpEditorBodyHtml(null, 'series', true, formValues, false, null);
assert(!createForm.includes('id="wpStatus"') && !createForm.includes('option value="matrix"'));
assert(createForm.includes('id="wpQuadrant"') && createForm.includes('Метка Матрицы'));
assert(createForm.includes('id="wpScheduledEnd"') && createForm.includes('value="2026-10-15"'), 'the separate calendar end date is editable independently of the deadline');
assert(!createForm.includes('wpDestinationHint') && !createForm.includes('Задача будет добавлена'));
const existingTask = { id: 'existing-task', title: 'Уже создана', homeType: 'personal', assignee: 'Я', statusKey: 'waiting', priorityKey: 'normal', scheduledDate: null, tags: [], matrixQuadrant: null };
const editForm = api.wpEditorBodyHtml(existingTask, 'task', false, { ...formValues, title: existingTask.title, statusKey: 'waiting' }, false, existingTask);
assert(editForm.includes('id="wpStatus"'), 'editing an existing task must preserve status changes');
const legacyMatrixForm = api.wpEditorBodyHtml({ ...existingTask, homeType: 'matrix' }, 'task', false, { ...formValues, kind: 'matrix' }, false, { ...existingTask, homeType: 'matrix' });
assert(legacyMatrixForm.includes('Матрица · прежний список'), 'legacy Matrix tasks remain editable without restoring Matrix navigation');
const applied = { id: 'new-project-task', homeType: 'personal', statusKey: 'open', priorityKey: 'normal', tags: [], matrixQuadrant: null };
api.wpApplyTaskForm(applied, { ...formValues, title: 'Метка в проекте', kind: 'project', projectId: 'project-1', quadrant: 0, statusKey: 'open' }, { newTask: true });
assert.strictEqual(applied.homeType, 'project');
assert.strictEqual(applied.matrixQuadrant, 0);
assert.strictEqual(applied.statusKey, 'open');
assert.strictEqual(applied.scheduledEndDate, '2026-10-15');
assert.strictEqual(api.wpDeadlineText({ deadlineAt: '2026-10-19', statusKey: 'open' }), 'До: 19.10.2026');
const longTitle = 'Подготовить подробный план запуска нового продукта и проверить все контрольные материалы';
const taskMarkup = api.wpTaskCard({ id: 'display-task', title: longTitle, homeType: 'personal', assignee: 'Alex', deadlineAt: '2026-10-19', statusKey: 'open', priorityKey: 'normal', tags: [], matrixQuadrant: null }, { compact: true });
assert(taskMarkup.includes('class="wp-task-title wp-title-marquee-clip"'));
assert(taskMarkup.includes(`<span class="wp-title-marquee-text">${longTitle}</span>`));
assert(taskMarkup.includes('До: 19.10.2026'));
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
    { id: 'project-task', title: 'Проектная', kind: 'project', projectId: 'project-1', project: 'Проект', inbox: true, status: 'Готово', prio: 'Выполнено', prevPrio: 'Критично', deadline: '20.10.2026' },
    { id: 'inbox-task', title: 'Входящая', inbox: true, status: 'В работе' },
    { id: 'personal-task', title: 'Личная', kind: 'personal', qm: 0, scheduledDate: '2026-10-10', scheduledEndDate: '2026-10-12', customField: 'keep me' }
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
const backup = JSON.parse(data.get(backupKey));
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
assert.strictEqual(api.wpTaskHomeType(inboxTask), 'inbox');
assert.strictEqual(api.wpTaskHomeType(personalTask), 'personal', 'a quadrant alone must not move a personal task into Matrix');
assert.strictEqual(personalTask.legacy.customField, 'keep me', 'unknown legacy fields must survive migration');
assert.strictEqual(personalTask.scheduledEndDate, '2026-10-12', 'the new calendar end date survives task migration');
assert(!Object.prototype.hasOwnProperty.call(personalTask.legacy, 'scheduledEndDate'), 'the end date is a first-class task field, not opaque legacy metadata');
assert.strictEqual(api.wpTaskHomeType(tasks.find(t => t.id === 'matrix-task')), 'matrix');
assert.strictEqual(api.wpTaskHomeType(tasks.find(t => t.id === 'loose-matrix-task')), 'matrix');
assert.strictEqual(state.board.taskSchemaVersion, 6);

assert.strictEqual(api.wpFinalizeTaskMigration(), true);
assert.strictEqual(state.taskMigrationOK, true);
assert.strictEqual(state.taskMigrationPending, false);
assert.strictEqual(JSON.parse(data.get('shelter:board')).taskSchemaVersion, 6);
assert.strictEqual(JSON.parse(data.get('shelter:boards')).customRegistryField, 'preserve');
assert.strictEqual(data.has('shelter:eisenTasks'), false, 'legacy standalone matrix data is removed only after verified v6 persistence');
assert(data.has(backupKey), 'the original migration backup remains available after migration');

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

assert.strictEqual(typeof api.render.tasks, 'function');
assert.strictEqual(typeof api.actions.taskEdit, 'function');
assert.strictEqual(typeof api.actions.wpView, 'function');
console.log('UI task planner, recurrence, classification, and guarded-migration tests passed');
