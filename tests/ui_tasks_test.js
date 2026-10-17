'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');
const marker = '/* =========================================================\n   SHELTER · Планировщик задач (v7 · дедлайн завершает диапазон)';
const start = html.indexOf(marker);
assert(start >= 0, 'the v7 planner module must be installed in the active UI');
const end = html.indexOf('\ninit();\n})();', start);
assert(end > start, 'the planner module must load before the application initializes');
const planner = html.slice(start, end);

assert(html.includes('<style id="shelter-week-planner-style">'), 'the task planner stylesheet must be present');
const plannerStyle = html.match(/<style id="shelter-week-planner-style">([\s\S]*?)<\/style>/);
assert(plannerStyle && plannerStyle[1].includes('.wp-filter-layout{display:grid;'), 'the list and board filters share a responsive grid');
assert(plannerStyle[1].includes('@container shelter (max-width:620px)'), 'the shared filter grid adapts to the app viewport');
const weekDayCss = html.match(/\.wp-week-grid\.is-all-day \.wp-day\{([^}]*)\}/);
assert(weekDayCss && /border:0/.test(weekDayCss[1]) && !/border-right:1px/.test(weekDayCss[1]), 'week columns must remain open without permanent day separators');
assert(html.includes('wpMigrateTaskStores(breg)'), 'legacy task migration must run during load');
assert(planner.includes('function wpFinalizeTaskMigration()'), 'migration must require an explicit confirmation step');
assert(planner.includes('debounce(e=>{WP.query=e.target.value;wpRenderView();},150)'), 'task search must be debounced by 150 ms');
assert(planner.includes("if(page && page.querySelector('#wpView') && !page.querySelector('.wp-migration-error')) { wpRenderView(); return; }"), 'task-page updates patch the existing planner view instead of replacing the page');
assert(planner.includes('restored.focus({preventScroll:true})') && planner.includes('setSelectionRange(focusState.start,focusState.end)'), 'planner updates restore focused controls and text selection');
assert(planner.includes("const search=page&&page.querySelector('#wpSearch'); if(search&&search.value!==WP.query) search.value=WP.query;"), 'view changes preserve the planner search query');
assert(planner.includes("kb('⌘⇧K')"), 'the task search shortcut label must adapt to the platform');
assert(planner.includes("await wpAddProjectFromEditor(target,projectSelection)") && planner.includes('wpPersistBoard()'), 'the task editor persists a project selected from its add-project option');
assert(planner.includes("kind==='deadline'?'Крайний срок':'Дата начала'") && !planner.includes('Плановая дата'), 'the planner consistently calls the scheduled start date “Дата начала”');
assert(planner.includes('plannerCalendarShowCompletedTasks') && planner.includes('plannerBoardShowCompletedTasks'), 'calendar and board completed-visibility preferences persist independently');
assert(!planner.includes('WP.showCompleted'), 'completed-visibility state is not shared across planner views');

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
const kb = s => s;
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
  wpTaskHomeType, wpIsMine, wpAssigneeLabel, wpAssigneeChoices, wpCanCommentTask, wpTaskCommentList, wpAddTaskComment, wpCommentThreadHtml, wpCalendarEntities, wpWeekEntities, wpMonthEntities, wpRangeEntities, wpCreateTask, wpApplyTaskForm,
  wpTaskScheduleStart, wpTaskScheduleEnd, wpTaskScheduleOffset, wpTaskCoversDate, wpTaskHasDateSpan, wpEffectiveOccurrence, wpApplySchedule, wpCalendarShownEntities, wpToggleCalendarCompleted,
  wpDeadlineText, wpTaskCard, wpDayColumn, wpCalendarSpanLayout, wpWeekCalendar, wpMonthEvent, wpMonthDay, wpMonthWeek, wpSetupTaskTitleMarquee, wpEditorProjectOptions, wpEditorBaseValues, wpEditorBodyHtml, wpHeaderControls, wpNavHtml, wpViewHtml,
  wpEnsureState, wpSetCalendarMode, wpCalendarMove, wpToggleSidebar, wpCalendarCompletedToggle, wpSelectCalendarDate,
  wpBoardEntities, wpBoardToolbar, wpAllFilterToolbar, wpRenderTaskPageFull, wpRenderView,
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
const assigneeChoices = Array.from(api.wpAssigneeChoices());
assert.strictEqual(assigneeChoices.filter(name => name === 'Alex').length, 1, 'self aliases collapse into one account-name option');
for (const alias of ['Вы', 'Я', 'вы', 'я', 'YOU', 'me', 'alex@example.test']) assert(!assigneeChoices.includes(alias), `assignee choices must not expose the self alias ${alias}`);
const selfAliasCard = api.wpTaskCard(state.board.tasks.find(task => task.id === 'self-ya'), { compact: true });
assert(selfAliasCard.includes('Исполнитель: Alex') && selfAliasCard.includes('<span>Alex</span>'), "compact cards display the signed-in user's name instead of Вы/Я");
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
assert(monthMarkup.includes('Исполнитель Я') && monthMarkup.includes('Проектная задача Вы') && monthMarkup.includes('Задача второго проекта Я') && !monthMarkup.includes('Чужая задача'));
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
assert(rangeMonthMarkup.includes('wp-month-span-layer') && rangeMonthMarkup.includes('wp-calendar-span--month'));
assert(rangeMonthMarkup.includes('wp-calendar-span wp-calendar-span--month p-normal'), 'the month range row is tinted by task priority');
assert(rangeMonthMarkup.includes('data-act="wpCalendarToggleCompleted"') && rangeMonthMarkup.includes('Скрыть выполненные задачи'), 'month calendar offers the completed-task visibility toggle');
assert(rangeMonthMarkup.includes('Задача на несколько дней'), 'month mode shows a continuous row for each multi-day task');
assert(rangeMonthMarkup.includes('Срок на границе месяца') && rangeMonthMarkup.includes('25 сент. 2026 — 2 окт. 2026'), 'month rows show ranges that cross the month boundary and retain the full deadline range');
assert(rangeMonthMarkup.includes('wp-month-selected') && rangeMonthMarkup.includes('Срок на границе месяца'), 'the selected-date list may keep a normal card for a task inside its span');
assert(rangeMonthMarkup.includes('data-act="wpQuickDay"'), 'month dates retain their own add-task controls');
const weekDays = Array.from({ length: 7 }, (_, i) => api.wpAddDays('2026-10-12', i));
const countId = (markup, id, attribute='data-id') => (markup.match(new RegExp(`${attribute}="${id}"`, 'g')) || []).length;
const monthWeekMarkup = api.wpMonthWeek(octoberGrid.slice(14, 21), rangeTasks);
assert.strictEqual(countId(monthWeekMarkup, 'week-span'), 1, 'a multi-day task is rendered once as a row, not copied into each date');
assert.strictEqual(countId(monthWeekMarkup, 'month-span-a'), 1, 'overlapping ranges each have one row in the month week');
assert.strictEqual(countId(monthWeekMarkup, 'month-span-b'), 1, 'the end date is included in the horizontal range');
assert.strictEqual(countId(monthWeekMarkup, 'single-day-task', 'data-id'), 1, 'a single-day task remains a normal event in its date cell');
assert(monthWeekMarkup.includes('wp-calendar-span wp-calendar-span--month p-critical'), 'month span tint follows critical priority even when its Matrix quadrant differs');
assert(monthWeekMarkup.includes('wp-calendar-span wp-calendar-span--month p-none'), 'no-priority ranges use a neutral tint');
assert(monthWeekMarkup.includes('Приоритет: Критичный') && monthWeekMarkup.includes('Приоритет: Без приоритета'), 'range labels expose the priority independently of Matrix classification');
const weekLayout = api.wpCalendarSpanLayout(weekDays, rangeTasks);
assert.strictEqual(weekLayout.laneCount, 2, 'overlapping all-day ranges occupy separate horizontal lanes');
assert.strictEqual(weekLayout.segments.length, 3, 'only tasks with a date range become horizontal spans');
assert(weekLayout.segments.find(segment => segment.task.id === 'week-span').continuesBefore, 'week range rows clip cleanly at the week boundary');
const rangeWeekCards = api.wpWeekCalendar(weekDays, rangeTasks);
assert(rangeWeekCards.includes('wp-calendar-span-layer wp-week-span-layer'), 'week mode restores its horizontal multi-day layer');
assert.strictEqual(countId(rangeWeekCards, 'week-span'), 1, 'the week view renders each spanning task once, not as a card in each covered day');
assert.strictEqual(countId(rangeWeekCards, 'month-span-a'), 1);
assert.strictEqual(countId(rangeWeekCards, 'month-span-b'), 1);
assert.strictEqual(countId(rangeWeekCards, 'single-day-task', 'data-task-id'), 1, 'single-day tasks remain cards in their own week cell');
assert(rangeWeekCards.includes('wp-calendar-span wp-calendar-span--week p-high'), 'week span color follows urgency rather than its Matrix quadrant');
assert(rangeWeekCards.includes('wp-day-span-spacer'), 'week days reserve room for the horizontal range lanes');
const desktopDay = api.wpDayColumn('2026-10-15', rangeTasks);
assert(desktopDay.includes('data-act="wpCalendarSelectDate" data-date="2026-10-15"'), 'clicking the whole week day selects it, not just its date label');
assert.strictEqual(countId(desktopDay, 'month-span-a'), 0, 'desktop date cells do not duplicate a range that is shown in the span layer');
assert.strictEqual(countId(desktopDay, 'single-day-task', 'data-task-id'), 1);
const monthDayCell = api.wpMonthDay('2026-10-15', rangeTasks);
assert(monthDayCell.includes('data-act="wpCalendarSelectDate" data-date="2026-10-15"'), 'clicking anywhere in a month day cell selects its date');
const selectedMobileDay = api.wpDayColumn('2026-10-15', rangeTasks, { includeMultiDay: true });
assert.strictEqual(countId(selectedMobileDay, 'month-span-a', 'data-task-id'), 1, 'the selected/mobile day list may keep ordinary multi-day cards');
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
assert(completedMonthMarkup.includes('wp-calendar-span wp-calendar-span--month p-high is-done'));
assert(completedMonthMarkup.includes('wp-month-event wp-title-marquee-clip p-normal is-done'), 'completed one-day month events keep their task state');
assert(completedMonthMarkup.includes('Статус: выполнена'), 'calendar controls announce completed status to assistive technology');
assert(api.wpCalendarCompletedToggle('calendar').includes('data-scope="calendar"') && api.wpCalendarCompletedToggle('calendar').includes('aria-pressed="true"'));
assert(api.wpCalendarCompletedToggle('board').includes('data-scope="board"') && api.wpCalendarCompletedToggle('board').includes('aria-pressed="true"'));
api.WP.calendarShowCompleted = false;
const hiddenCompletedWeek = api.wpWeekCalendar(weekDays, completedCalendarTasks);
const hiddenCompletedMonth = api.wpMonthWeek(octoberGrid.slice(14, 21), completedCalendarTasks);
const hiddenCompletedMobile = api.wpDayColumn('2026-10-15', completedCalendarTasks, { includeMultiDay: true });
assert(!hiddenCompletedWeek.includes('done-span') && !hiddenCompletedWeek.includes('done-single'), 'the calendar toggle hides completed week rows and cards');
assert(!hiddenCompletedMonth.includes('done-span') && !hiddenCompletedMonth.includes('done-single'), 'the calendar toggle hides completed month rows and events');
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
  { id:'board-cancelled',title:'Отменена',homeType:'personal',assignee:'Alex',statusKey:'cancelled',priorityKey:'normal',order:3 }
];
state.board.occurrences=[]; api.WP.query=''; api.WP.boardHomeFilter='all'; api.WP.boardAssigneeFilter='all'; api.WP.boardFocus='all';
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
assert(api.WP.calendarShowCompleted, 'the board toolbar keeps the completed calendar filter untouched');
const allToolbar=api.wpAllFilterToolbar();
assert(allToolbar.includes('wp-filter-layout wp-all-filter-row') && allToolbar.includes('wp-filter-main'), 'all tasks uses the same filter-grid structure');
assert(allToolbar.indexOf('wp-filter-assignee')<allToolbar.indexOf('wp-filter-project'), 'assignee and project filters keep the same order in both views');
assert(boardToolbar.indexOf('wp-filter-assignee')<boardToolbar.indexOf('wp-filter-project'));
api.WP.allFilter='active'; api.WP.boardShowCompleted=true; state.board.tasks=priorBoardTasks; state.board.occurrences=priorOccurrences;

api.WP.monthStart = '2026-10-01'; api.WP.mobileDate = '2026-10-01'; api.WP.calendarMode = 'month';
const monthView = api.wpViewHtml();
assert(monthView.includes('wp-month-span-layer') && monthView.includes('wp-calendar-span--month'));
const rangeMonthWeek = monthView.slice(monthView.indexOf('<div class="wp-month-grid"'));
assert.strictEqual(countId(rangeMonthWeek, 'span-before-month'), 1, 'a range starting before the month grid is clipped and shown once');
assert(rangeMonthWeek.includes('continues-before'), 'a clipped month row indicates continuation from the previous week');
api.WP.weekStart = '2026-10-12'; api.WP.mobileDate = '2026-10-15'; api.WP.calendarMode = 'week';
const rangeWeekMarkup = api.wpViewHtml();
assert(rangeWeekMarkup.includes('wp-week-grid is-all-day') && rangeWeekMarkup.includes('wp-week-span-layer'));
assert(!rangeWeekMarkup.includes('Входящие без плана') && !rangeWeekMarkup.includes('wp-inbox-shelf'), 'the week view replaces the unplanned inbox shelf');
const weekSelectedAt = rangeWeekMarkup.indexOf('<section class="wp-week-selected"');
assert(weekSelectedAt >= 0, 'the week view shows a selected-day list below the calendar');
const weekSelectedPanel = rangeWeekMarkup.slice(weekSelectedAt, rangeWeekMarkup.indexOf('</section>', weekSelectedAt) + '</section>'.length);
assert(weekSelectedPanel.includes('month-span-a') && weekSelectedPanel.includes('month-span-b') && weekSelectedPanel.includes('single-day-task'), 'the week selected-day list includes every task covering that date, including multi-day tasks');
assert(rangeWeekMarkup.includes('Однодневная задача') && rangeWeekMarkup.includes('Задача на несколько дней'));
const desktopWeekGrid = rangeWeekMarkup.slice(rangeWeekMarkup.indexOf('<div class="wp-week-grid is-all-day">'), weekSelectedAt);
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
assert(!api.wpHeaderControls().includes('data-act="wpQuickGlobal"'), 'the global add button is hidden in week mode');
api.WP.calendarMode = 'month';
assert(!api.wpHeaderControls().includes('data-act="wpQuickGlobal"'), 'the global add button is hidden in month mode');
api.WP.view = 'inbox';
assert(api.wpHeaderControls().includes('data-act="wpQuickGlobal"'), 'the global add button remains in non-calendar views');
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
assert.strictEqual(createdPersonal.assignee, 'Alex', 'new tasks persist the canonical account name rather than a self alias');
assert.strictEqual(createdPersonal.matrixQuadrant, 2);
assert.strictEqual(createdPersonal.statusKey, 'open');
const formValues = {
  title: 'Новая задача', kind: 'personal', projectId: '', assignee: 'Я', scheduledDate: '2026-10-13', deadlineAt: '2026-10-15',
  statusKey: 'open', priorityKey: 'normal', quadrant: '1', description: '', tags: '', durationEstimateMinutes: '',
  waitingReason: '', reviewAt: '', frequency: 'none', interval: 1, intervalDays: 1, weekdays: [1], mode: 'calendar', endDate: ''
};
const createForm = api.wpEditorBodyHtml(null, 'series', true, formValues, false, null);
assert(createForm.includes('<span>Дата начала</span>') && !createForm.includes('Плановая дата'), 'the task form labels the schedule as its start date');
assert(createForm.includes('<option value="__add">+ Добавить новый проект</option>'), 'new-task project selector always offers project creation');
const projectsBeforeEmptyForm = state.board.projects;
state.board.projects = [];
const emptyProjectForm = api.wpEditorBodyHtml(null, 'series', true, { ...formValues, kind: 'project', projectId: '' }, false, null);
assert(emptyProjectForm.includes('id="wpProject"') && emptyProjectForm.includes('+ Добавить новый проект'), 'the add-project action remains available when the board has no projects');
state.board.projects = projectsBeforeEmptyForm;
assert(!createForm.includes('id="wpStatus"') && !createForm.includes('option value="matrix"'));
assert(createForm.includes('id="wpQuadrant"') && createForm.includes('Метка Матрицы'));
assert(!createForm.includes('wpScheduledEnd') && !createForm.includes('Дата окончания · для календаря') && createForm.includes('id="wpDeadline"'), 'the existing deadline is the only end-date field in the form');
assert(!createForm.includes('wpDestinationHint') && !createForm.includes('Задача будет добавлена'));
const existingTask = { id: 'existing-task', title: 'Уже создана', homeType: 'personal', assignee: 'Я', statusKey: 'waiting', priorityKey: 'normal', scheduledDate: null, tags: [], matrixQuadrant: null };
const editForm = api.wpEditorBodyHtml(existingTask, 'task', false, { ...formValues, title: existingTask.title, statusKey: 'waiting' }, false, existingTask);
assert(editForm.includes('id="wpStatus"'), 'editing an existing task must preserve status changes');
const legacyMatrixForm = api.wpEditorBodyHtml({ ...existingTask, homeType: 'matrix' }, 'task', false, { ...formValues, kind: 'matrix' }, false, { ...existingTask, homeType: 'matrix' });
assert(legacyMatrixForm.includes('Матрица · прежний список'), 'legacy Matrix tasks remain editable without restoring Matrix navigation');
const applied = { id: 'new-project-task', homeType: 'personal', statusKey: 'open', priorityKey: 'normal', tags: [], matrixQuadrant: null };
api.wpApplyTaskForm(applied, { ...formValues, title: 'Метка в проекте', kind: 'project', projectId: 'project-1', quadrant: 0, statusKey: 'open' }, { newTask: true });
assert.strictEqual(applied.homeType, 'project');
assert.strictEqual(applied.assignee, 'Alex', 'saving an alias from an older form stores the account name');
assert.strictEqual(applied.matrixQuadrant, 0);
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
assert(taskMarkup.includes('class="wp-task qnone p-normal'), 'task cards expose the priority class used for urgency coloring');
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
assert.strictEqual(projectTask.comments[0].body, 'Сохранённый комментарий', 'comments survive legacy task migration');
assert(!Object.prototype.hasOwnProperty.call(projectTask.legacy, 'comments'), 'comments are preserved as task data rather than duplicated as legacy metadata');
assert.strictEqual(api.wpTaskHomeType(inboxTask), 'inbox');
assert.strictEqual(api.wpTaskHomeType(personalTask), 'personal', 'a quadrant alone must not move a personal task into Matrix');
assert.strictEqual(personalTask.legacy.customField, 'keep me', 'unknown legacy fields must survive migration');
assert.strictEqual(personalTask.deadlineAt, '2026-10-12', 'a legacy calendar end migrates into the existing deadline field');
assert(!Object.prototype.hasOwnProperty.call(personalTask, 'scheduledEndDate'), 'the migrated task no longer stores a separate calendar end date');
assert(!Object.prototype.hasOwnProperty.call(personalTask.legacy, 'scheduledEndDate'), 'the legacy end date is normalized, not retained as opaque metadata');
assert.strictEqual(tasks.find(t => t.id === 'deadline-wins').deadlineAt, '2026-10-14', 'an already-set deadline takes precedence over the legacy calendar end');
assert(!Object.prototype.hasOwnProperty.call(tasks.find(t => t.id === 'deadline-wins'), 'scheduledEndDate'));
assert.strictEqual(api.wpTaskHomeType(tasks.find(t => t.id === 'matrix-task')), 'matrix');
assert.strictEqual(api.wpTaskHomeType(tasks.find(t => t.id === 'loose-matrix-task')), 'matrix');
assert.strictEqual(state.board.taskSchemaVersion, 7);

assert.strictEqual(api.wpFinalizeTaskMigration(), true);
assert.strictEqual(state.taskMigrationOK, true);
assert.strictEqual(state.taskMigrationPending, false);
assert.strictEqual(JSON.parse(data.get('shelter:board')).taskSchemaVersion, 7);
assert.strictEqual(JSON.parse(data.get('shelter:boards')).customRegistryField, 'preserve');
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
const savedV7Board = JSON.parse(data.get('shelter:board'));
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

assert.strictEqual(typeof api.render.tasks, 'function');
assert.strictEqual(typeof api.actions.taskEdit, 'function');
assert.strictEqual(typeof api.actions.wpView, 'function');
console.log('UI task planner, recurrence, classification, and guarded-migration tests passed');
