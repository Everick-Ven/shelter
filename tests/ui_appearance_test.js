'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');
const section = (from, to) => {
  const start = html.indexOf(from);
  assert(start >= 0, `missing source marker: ${from}`);
  const end = to ? html.indexOf(to, start + from.length) : html.length;
  assert(end >= 0, `missing end marker: ${to}`);
  return html.slice(start, end);
};

// Graphite is the first-run fallback, while an already saved selection is read as-is.
assert.match(html, /<html lang="ru" data-theme="graphite">/);
assert.match(html, /theme: 'graphite', ghost: false/);
assert.match(html, /let th = store\.get\('shelter:theme'\); th = map\[th\] \|\| th; S\.theme = THEMES\[th\] \? th : 'graphite';/);
assert.match(html, /const fallback = mode === 'light' \? 'ivory' : 'graphite';/);
const themeOrder = html.match(/const THEME_ORDER = \[[^\]]+\];/);
assert(themeOrder && themeOrder[0].includes("'graphite'"));
assert(themeOrder[0].indexOf("'graphite'") < themeOrder[0].indexOf("'obsidian'"));

// Named themes and light/dark mode can be changed through Settings only.
const quickPanel = section('function quickPanelHtml() {', 'function syncQuickPanel(full) {');
assert(!quickPanel.includes('schemeMode'));
assert(!quickPanel.includes('Цветовая тема'));
assert(!quickPanel.includes('data-act="theme"'));
const commands = section('const COMMANDS = () => [', 'function paletteItems(q) {');
assert(!commands.includes('setTheme('));
assert(!commands.includes('THEME_ORDER'));
assert(!html.includes('function themeMenu('));
assert(!html.includes('function themeSwatchesMarkup('));
const settingsStart = html.indexOf('function settingsHtml() {');
const settingsEnd = html.indexOf('function openSettings(sec) {', settingsStart);
assert(settingsStart >= 0 && settingsEnd > settingsStart);
const settings = html.slice(settingsStart, settingsEnd);
assert(settings.includes('class="theme-cards"'));
assert(settings.includes("segHtml('schemeMode'"));
assert(settings.includes("['graphite', 'Графит']"));
const themeActionAt = html.indexOf('data-act="theme"');
assert(themeActionAt > settingsStart && themeActionAt < settingsEnd, 'the theme picker must be rendered inside Settings');
assert.strictEqual(html.indexOf('data-act="theme"', themeActionAt + 1), -1, 'no second theme-picker entry point should remain');
assert.match(html, /theme: el => \{ if \(el\.closest\('\.settings'\)\) setTheme\(el\.dataset\.theme, el\); \}/);
assert.strictEqual((html.match(/segHtml\('schemeMode'/g) || []).length, 1, 'light/dark mode is rendered only in Settings');

// Planner title, content gutters, and layout respond to the app's own viewport.
const plannerHeader = section('function wpHeaderControls() {', 'function wpDateControls() {');
assert(plannerHeader.includes("${ico('tasks')}Задачи"));
assert(plannerHeader.includes('class="wp-title-marquee-clip"'));
assert(!plannerHeader.includes('S.board.name'));
assert(!plannerHeader.includes("ico('layers')"));
const plannerStyleStart = html.indexOf('<style id="shelter-week-planner-style">');
const plannerStyleEnd = html.indexOf('</style>', plannerStyleStart);
assert(plannerStyleStart >= 0 && plannerStyleEnd > plannerStyleStart);
const plannerCss = html.slice(plannerStyleStart, plannerStyleEnd);
const plannerPageRules = [...plannerCss.matchAll(/\.planner-page\{([^}]*)\}/g)].map(match => match[1]);
assert(plannerPageRules.length >= 5, 'planner padding must be responsive across the supported breakpoints');
assert(plannerPageRules.every(rule => {
  const padding = (rule.match(/(?:^|;)padding:([^;]+)/) || [])[1] || '';
  const values = padding.replace(/clamp\([^)]*\)/g, 'clamp').trim().split(/\s+/);
  return values.length === 3 && ['12px', 'clamp'].includes(values[1]);
}), 'the task page must keep a modest responsive horizontal gutter');
assert(plannerCss.includes('.planner-page{padding:clamp(22px,2.4vw,38px) clamp(12px,1.3vw,18px) 56px;'));
assert(plannerCss.includes('.wp-layout.is-sidebar-collapsed{grid-template-columns:68px minmax(0,1fr);gap:16px}'));
assert(plannerCss.includes('.wp-rail-toggle{display:grid;place-items:center;'));
assert(plannerCss.includes('.wp-month-dows,.wp-month-days{display:grid;grid-template-columns:repeat(7,minmax(0,1fr));gap:10px}'));
assert(plannerCss.includes('.wp-month-grid{display:flex;flex-direction:column;gap:10px;min-width:0}'));
assert(plannerCss.includes('.wp-month-week{position:relative;min-width:0}'));
assert(plannerCss.includes('.wp-month-span-layer{top:40px;left:8px;right:8px}'));
assert(plannerCss.includes('.wp-month-day.is-today{'));
assert(plannerCss.includes('.wp-month-selected{'));
assert(plannerCss.includes('.wp-calendar-span-layer{position:absolute;display:grid;grid-template-columns:repeat(7,minmax(0,1fr));'));
assert(plannerCss.includes('.wp-calendar-span.continues-before'));
assert(plannerCss.includes('.wp-week-grid.is-all-day{position:relative;display:block;'));
assert(plannerCss.includes('.wp-week-grid.is-all-day .wp-day-head{'));
assert(plannerCss.includes('.wp-week-span-layer{top:60px;left:0;right:0;column-gap:0}'));
assert(plannerCss.includes('.wp-quick-quadrant{'));
const taskTitleRule = (plannerCss.match(/(?:^|})\.wp-task-title\{([^}]*)\}/) || [])[1] || '';
assert(taskTitleRule.includes('white-space:nowrap') && taskTitleRule.includes('text-overflow:ellipsis') && !taskTitleRule.includes('overflow-wrap:anywhere'));
assert(plannerCss.includes('@keyframes wpTaskTitleMarquee'));
assert(plannerCss.includes('.wp-title-marquee-clip.is-title-overflowing:hover'));
assert(plannerCss.includes('.wp-task-meta .wp-deadline{flex:none;white-space:nowrap;font-variant-numeric:tabular-nums;'));
assert(!plannerCss.includes('.wp-destination-hint{') && !plannerCss.includes('.wp-quick-context{'));
assert(plannerCss.includes('.wp-layout{display:grid;grid-template-columns:170px minmax(0,1fr);gap:22px;align-items:start;width:100%;max-width:none;margin:0}'));
assert(plannerCss.includes('.wp-week-grid{display:grid;grid-template-columns:repeat(7,minmax(0,1fr));gap:10px;min-width:0;overflow:visible;'));
assert(!plannerCss.includes('.wp-week-grid{min-width:1005px') && !plannerCss.includes('.wp-week-grid{min-width:1035px'), 'the weekly calendar must not force horizontal overflow');
assert(plannerCss.includes('.wp-day{display:flex;flex-direction:column;gap:6px;min-width:0;min-height:420px;padding:10px 8px;border:1px solid var(--line);border-radius:calc(16px*var(--rs));background:var(--card);'));
assert(plannerCss.includes('.wp-week-grid .wp-task{grid-template-columns:26px minmax(0,1fr) 30px;padding:7px 8px;border:0;border-left:2px solid rgb(var(--accent));border-radius:calc(10px*var(--rs));background:var(--card-2);'));
assert(plannerCss.includes('@container shelter (max-width:1250px){'));
assert(plannerCss.includes('@container shelter (max-width:900px){'));
assert(plannerCss.includes('@container shelter (max-width:720px){'));
assert(plannerCss.includes('@container shelter (max-width:620px){'));
assert(plannerCss.includes('.wp-week-grid{grid-template-columns:repeat(4,minmax(0,1fr))}'));
assert(plannerCss.includes('.wp-week-grid{grid-template-columns:repeat(2,minmax(0,1fr))}'));
assert(plannerCss.includes('.wp-week-grid{grid-template-columns:1fr}.wp-week-grid .wp-day{min-height:220px}.wp-week-mobile{display:none}'));
assert(plannerCss.includes('.wp-week-grid{display:grid;grid-template-columns:1fr;min-width:0;overflow:visible}.wp-week-mobile{display:none}'));
assert(!plannerCss.includes('.wp-week-grid{display:none}.wp-week-mobile{display:block}'));
assert(plannerCss.includes('.wp-week-grid.is-all-day{display:none}'));
assert(plannerCss.includes('.wp-week-mobile{display:block}'));
const narrowCalendarContainer = plannerCss.slice(plannerCss.lastIndexOf('@container shelter (max-width:620px){'), plannerCss.lastIndexOf('@media(max-width:620px){'));
assert(narrowCalendarContainer.includes('.wp-week-grid.is-all-day{display:none}.wp-week-mobile{display:block}'), 'narrow planner containers retain the selected-day week view');
assert(plannerCss.includes('@container shelter (max-width:560px){.planner-page{padding:18px 12px 36px}}'));
const plannerNav = section('function wpNavHtml() {', 'function wpContextAddLabel() {');
assert(!plannerNav.includes('Матрица') && !plannerNav.includes("wpSetView('matrix')"), 'Matrix must not remain a standalone navigation destination');
const plannerViewConfig = section('const WP_VIEWS=[', 'function wpViewTitle() {');
assert(plannerViewConfig.includes("['week','calendar','Календарь']") && !plannerViewConfig.includes("['matrix'"));
const plannerMoreMenu = section('function wpMoreMenu(anchor) {', 'function taskExport() {');
assert(plannerMoreMenu.includes("label:'Календарь'") && !plannerMoreMenu.includes('Матрица') && !plannerMoreMenu.includes("wpSetView('matrix')"));
const legacyMovePicker = section('function wpMoveHomePicker(ref,opts={}) {', 'function wpMoveToQuadrant(ref,q) {');
assert(legacyMovePicker.includes("value=\"legacy-matrix\"") && legacyMovePicker.includes('selected disabled'));
assert(!legacyMovePicker.includes("['matrix','Матрица']"), 'legacy Matrix tasks must not restore Matrix as a move destination');

console.log('UI appearance defaults, Settings-only theme controls, and planner responsiveness tests passed');
