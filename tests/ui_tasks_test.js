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
const RENDER = {}, AFTER = {}, ACT = {};
const OV = { stack: [] };
const document = { activeElement: null, addEventListener() {} };
const window = {};
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
const context = { __state: state, __data: data, __denyBackup: false, console, setTimeout, clearTimeout };
vm.runInNewContext(harness + '\n' + planner + `
globalThis.__api = {
  schema: WP_SCHEMA, WP, S, store, actions: ACT, render: RENDER, after: AFTER,
  wpDateKey, wpWeekStart, wpAddDays, wpMatchRuleDate, wpTaskHomeType,
  wpNormalizeBoard, wpMigrateTaskStores, wpFinalizeTaskMigration
};`, context, { timeout: 5000 });
const api = context.__api;

// Date arithmetic stays local-date based and rejects impossible calendar dates.
assert.strictEqual(api.wpDateKey('13.10.2026'), '2026-10-13');
assert.strictEqual(api.wpDateKey('2026-02-30'), '');
assert.strictEqual(api.wpWeekStart('2026-10-13'), '2026-10-12');
assert.strictEqual(api.wpAddDays('2026-12-31', 1), '2027-01-01');

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
    { id: 'personal-task', title: 'Личная', kind: 'personal', qm: 0, customField: 'keep me' }
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
