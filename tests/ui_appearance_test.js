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
  const values = padding.trim().split(/\s+/);
  return values.length === 3 && values[1] === '0';
}), 'the task page must not add broad horizontal gutters');
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
assert(plannerCss.includes('@container shelter (max-width:560px){.planner-page{padding:18px 0 36px}}'));

console.log('UI appearance defaults, Settings-only theme controls, and planner responsiveness tests passed');
