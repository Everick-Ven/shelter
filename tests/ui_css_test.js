// Регрессия целостности таблицы стилей UI.
//
// Один незакрытый «:is(...» в селекторе стоил дорого: парсер Chromium считает
// такой прелюд началом правила и съедает остаток таблицы стилей — весь хвост
// (включая блок «отступы каркаса», тулбар, сцену и воздух между ними) тихо
// перестаёт применяться. Внешне это выглядит как «всё слиплось», а в исходнике
// скобки {} сбалансированы, поэтому обычная проверка этого не замечает.
//
// Тест разбирает <style> из resources/ui/index.html, как это сделал бы браузер
// (комментарии, строки и вложенность учитываются), и требует:
//   1) баланса круглых/квадратных/фигурных скобок во всём блоке;
//   2) баланса скобок в прелюде каждого правила верхнего уровня;
//   3) наличия «якорей» хвоста таблицы стилей — если хвост обрублен, они
//      исчезают из файла.
'use strict';
const fs = require('node:fs');
const path = require('node:path');

const file = path.join(__dirname, '..', 'resources', 'ui', 'index.html');
const html = fs.readFileSync(file, 'utf8');

const results = [];
const check = (name, ok, detail) => {
  results.push(!!ok);
  console.log((ok ? 'PASS ' : 'FAIL ') + name + (detail !== undefined ? ' :: ' + detail : ''));
};
const fail = name => { check(name, false); finish(); };

let finished = false;
function finish() {
  if (finished) return;
  finished = true;
  const failed = results.filter(x => !x).length;
  console.log(failed ? 'UI_CSS_TEST_FAIL ' + failed : 'UI_CSS_TEST_PASS ' + results.length + '/' + results.length);
  process.exit(failed ? 1 : 0);
}

const styleMatches = [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)];
if (!styleMatches.length) fail('таблица стилей найдена в index.html');
const css = styleMatches.map(match => match[1]).join('\n');
check('таблицы стилей найдены в index.html', true, `${styleMatches.length} блоков, ${css.length} символов`);

// Токенизация: комментарии и строки не считаются структурой.
const structure = [];
let inComment = false;
let quote = '';
let line = 1;   // счётчик строк ведём по ходу, иначе разбор был бы квадратичным
for (let i = 0; i < css.length; i++) {
  const c = css[i];
  if (c === '\n') { line++; continue; }
  if (inComment) {
    if (c === '*' && css[i + 1] === '/') { inComment = false; i++; }
    continue;
  }
  if (quote) {
    if (c === '\\') { i++; continue; }
    if (c === quote) quote = '';
    continue;
  }
  if (c === '/' && css[i + 1] === '*') { inComment = true; i++; continue; }
  if (c === '"' || c === "'") { quote = c; continue; }
  structure.push({ ch: c, line: line });
}
check('лексика таблицы стилей читается (комментарии и строки пропущены)', structure.length > 0, structure.length + ' значимых символов');

// 1. Общий баланс.
const count = ch => structure.filter(s => s.ch === ch).length;
const parenOpen = count('('), parenClose = count(')');
const bracketOpen = count('['), bracketClose = count(']');
const braceOpen = count('{'), braceClose = count('}');
check('круглые скобки сбалансированы', parenOpen === parenClose, `${parenOpen} открыто, ${parenClose} закрыто`);
check('квадратные скобки сбалансированы', bracketOpen === bracketClose, `${bracketOpen} открыто, ${bracketClose} закрыто`);
check('фигурные скобки сбалансированы', braceOpen === braceClose, `${braceOpen} открыто, ${braceClose} закрыто`);

// 2. Прелюд каждого правила верхнего уровня: селектор обязан иметь закрытые
//    круглые и квадратные скобки. Именно здесь ловится «:is(...  без конца».
const ruleProblems = [];
let depth = 0;
let prelude = [];
let preludeStartLine = 1;
for (const token of structure) {
  if (depth === 0 && token.ch === '{') preludeStartLine = prelude.length ? prelude[0].line : token.line;
  if (token.ch === '{') {
    if (depth === 0) {
      const text = prelude.map(p => p.ch).join('').trim();
      if (text && !text.startsWith('@')) {
        const opens = (text.match(/\(/g) || []).length, closes = (text.match(/\)/g) || []).length;
        const bOpens = (text.match(/\[/g) || []).length, bCloses = (text.match(/\]/g) || []).length;
        if (opens !== closes) ruleProblems.push(`строка ${preludeStartLine}: ${text.slice(0, 90)} — круглых ${opens}/${closes}`);
        if (bOpens !== bCloses) ruleProblems.push(`строка ${preludeStartLine}: ${text.slice(0, 90)} — квадратных ${bOpens}/${bCloses}`);
      }
      prelude = [];
    }
    depth++;
    continue;
  }
  if (token.ch === '}') {
    depth--;
    if (depth < 0) fail('фигурные скобки не уходят в минус');
    if (depth === 0) prelude = [];
    continue;
  }
  if (depth === 0) prelude.push(token);
}
check('прелюды правил верхнего уровня закрыты корректно', ruleProblems.length === 0, ruleProblems.slice(0, 4).join(' | ') || 'проблем нет');
check('вложенность фигурных скобок возвращается к нулю', depth === 0, 'глубина ' + depth);

// 3. Якоря хвоста таблицы стилей: обрубленный хвост не может их содержать.
const anchors = [
  ['.main{padding-left:8px}', 'воздух панель↔контент'],
  ['.tb{margin:8px 0 10px}', 'зазор тулбар↔сцена'],
  ['.bm-mount:not([hidden]){margin:0 0 8px', 'зазор строки закладок'],
  ['.hero-links{padding:7px 12px}', 'паддинг ссылок героя'],
];
for (const [fragment, why] of anchors) {
  check('якорь хвоста стилей на месте: ' + why, css.includes(fragment));
}

// Sticky large-title header bleeds exactly through the page gutter, never past
// the dashboard's own width (desktop gutter 22 px, compact container 12 px).
const structureCss = (html.match(/<style id="ux-macos-structure">([\s\S]*?)<\/style>/) || [])[1] || '';
check('липкая шапка совпадает с полями страницы и не расширяет дашборд',
  /margin:-6px calc\(-1 \* clamp\(22px,2\.8vw,44px\)\) 18px/.test(structureCss) &&
  /\.dashboard-page > \.page-h\s*\{\s*margin-left:-12px;\s*margin-right:-12px;\s*padding-left:12px;\s*padding-right:12px;/.test(structureCss));
check('на узком экране инструменты возвращаются в переносимый flex-ряд',
  /@media \(max-width:720px\)\s*\{[\s\S]*?\.tb\{display:flex;height:auto;min-height:54px;flex-wrap:wrap/.test(structureCss) &&
  /\.tb>\.omni-tb\{flex:1 1 140px;width:auto;min-width:140px;max-width:none;margin:0\}/.test(structureCss) &&
  /\.tb>\.tb-actions\{flex:1 0 100%;min-width:0;flex-wrap:wrap;justify-content:flex-end;gap:5px\}/.test(structureCss));
check('узкий ассистент: detail занимает всю ширину при выносе рейла',
  /@container dt-shell \(max-width:900px\)\s*\{\s*\/\*[\s\S]*?\*\/\s*\.dt-app > \.dt-main\{grid-column:1 \/ -1\}/.test(css));

const sidebarStart = html.indexOf('<aside class="sb" id="sb"');
const sidebarEnd = html.indexOf('</aside>', sidebarStart);
const sidebar = sidebarStart >= 0 && sidebarEnd > sidebarStart ? html.slice(sidebarStart, sidebarEnd) : '';
const tabScrollStart = sidebar.indexOf('<div class="sb-scroll" id="sbScroll">');
const toolsStart = sidebar.indexOf('<div class="sb-tools" id="sbTools">');
const tabScroll = tabScrollStart >= 0 && toolsStart > tabScrollStart ? sidebar.slice(tabScrollStart, toolsStart) : '';
check('в левой панели прокручивается только область вкладок',
  tabScroll.includes('id="spaceRow"') && tabScroll.includes('id="tabList"') && !tabScroll.includes('data-page="widgets"') && toolsStart > tabScrollStart);
check('инструменты закреплены отдельно в нижней части левой панели',
  toolsStart >= 0 && ['widgets','dashboard','downloads','passwords','history','extensions'].every(page => sidebar.indexOf(`data-page="${page}"`) > toolsStart) &&
  css.includes('.sb-inner{position:relative;display:flex;flex:1 1 0%;flex-direction:column;min-height:0;overflow:hidden') &&
  css.includes('.sb-scroll{flex:0 1 auto;min-height:0;overflow-y:auto') &&
  css.includes('.sb-tools{flex:0 0 auto;margin:auto -12px 0'));
check('в свёрнутой панели подложка выделения пространства совпадает с плиткой иконки',
  css.includes('.app.collapsed .sb .space-row .hc{display:none}') &&
  css.includes('.app.collapsed .sb .space-row{justify-content:center;padding-left:0}') &&
  css.includes('.app.collapsed .sb .space-btn{flex:none;justify-content:center;padding:0 6px}') &&
  css.includes('#tabRight.is-collapsed .space-btn .nm,#tabRight.is-collapsed .space-btn .chev,#tabRight.is-collapsed .space-row>.icon-btn{display:none}'));
check('кнопки имеют центрированное содержимое, включая проверку кода импорта',
  /\.btn\{[^}]*text-align:center/.test(css) &&
  html.includes('.wp-import-password-submit-content{position:relative;display:block;width:100%;text-align:center') &&
  html.includes('.wp-import-password-submit-content>.i{position:absolute;left:0;top:50%') &&
  html.includes('wp-import-password-submit-content') && html.includes('<span>Проверить код</span>'));
const plannerStyle = (html.match(/<style id="shelter-week-planner-style">([\s\S]*?)<\/style>/) || [])[1] || '';
const taskLayoutStyle = (html.match(/<style id="shelter-task-layout">([\s\S]*?)<\/style>/) || [])[1] || '';
check('экран задач использует полноэкранный rail и прокрутку рабочего поля',
  /\.page\.planner-page\{\s*position:absolute;inset:0;width:100%;height:100%/.test(taskLayoutStyle) &&
  taskLayoutStyle.includes('.planner-page .wp-layout{') && taskLayoutStyle.includes('grid-template-columns:288px minmax(0,1fr)') &&
  taskLayoutStyle.includes('.planner-page .wp-rail{') && taskLayoutStyle.includes('width:auto;height:100%') &&
  taskLayoutStyle.includes('.planner-page .wp-view{') && taskLayoutStyle.includes('overflow:auto;overscroll-behavior:contain'));
check('новый rail задач содержит мобильный слой открытия и закрытия',
  taskLayoutStyle.includes('@container shelter (max-width:900px)') &&
  taskLayoutStyle.includes('.planner-page .wp-layout.is-rail-open .wp-rail') && taskLayoutStyle.includes('.wp-rail-scrim{') &&
  html.includes('class="wp-mobile-menu" data-act="wpRailOpen"') && html.includes('class="wp-rail-scrim" data-act="wpRailClose"'));
check('месячная сетка и выбранный день используют двухколоночную компоновку, как в «Истории»',
  taskLayoutStyle.includes('.wp-month-layout{') && taskLayoutStyle.includes('grid-template-columns:minmax(0,1fr) 330px') &&
  html.includes('<div class="wp-month-layout"><div class="wp-month-calendar">'));
check('мини-календарь закреплён слева, а подробный день занимает правую колонку',
  taskLayoutStyle.includes('.wp-month-layout.is-mini{grid-template-columns:232px minmax(0,1fr)}') &&
  taskLayoutStyle.includes('.wp-month-layout.is-mini>.wp-month-mini{position:sticky;top:0}') &&
  taskLayoutStyle.includes('.wp-month-layout.is-mini>.wp-month-selected{position:static;max-height:none;overflow:visible}') &&
  taskLayoutStyle.includes('.wp-month-layout.is-mini{grid-template-columns:minmax(0,1fr)}') &&
  html.includes('<div class="wp-month-layout is-mini">'));
check('доска делит общие боковые отступы с остальными представлениями, как в референсе',
  taskLayoutStyle.includes('.planner-page .wp-view:has(>.wp-board-lanes){overflow-x:clip}') &&
  !taskLayoutStyle.includes('wp-board-lanes){padding-right'));
check('сводка доски не закреплена и не перекрывает тулбар при прокрутке',
  !taskLayoutStyle.includes('.wp-board-pin') &&
  html.includes('.planner-page :is(.wp-all-toolbar,.wp-board-toolbar).is-scrollable{position:relative;top:auto;box-shadow:none}'));
check('рабочее поле задач резервирует место под полосу прокрутки, чтобы контент не ездил',
  taskLayoutStyle.includes('.planner-page .wp-view{') && taskLayoutStyle.includes('scrollbar-gutter:stable'));
check('доска задач вмещается в ширину экрана без горизонтальной прокрутки',
  plannerStyle.includes('.wp-board-lanes{display:grid;grid-template-columns:repeat(var(--wp-lanes,4),minmax(0,1fr));align-items:start;gap:9px;overflow-x:hidden') &&
  !plannerStyle.includes('repeat(4,minmax(220px,1fr))') && !/\.wp-board-lanes\{[^}]*overflow-x:auto/.test(plannerStyle) &&
  (plannerStyle.match(/\.wp-board-lanes\{grid-template-columns:repeat\(2,minmax\(0,1fr\)\);gap:7px\}/g) || []).length === 2 &&
  !/\.wp-board-lanes\{[^}]*scroll-snap/.test(plannerStyle) &&
  plannerStyle.includes('.wp-board-task-status .select{width:auto;min-width:min(115px,100%)') &&
  plannerStyle.includes('.wp-board-column>header b{font-size:13px;font-weight:620;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}'));
check('задачи дня и многодневные ряды держат одинаковые отступы от краёв и друг от друга',
  plannerStyle.includes('.wp-day-capsule{display:flex;flex-direction:column;align-items:stretch;gap:2px;width:auto;min-width:0;min-height:72px;margin:0 -6px;padding:5px 10px;') &&
  plannerStyle.includes('padding:5px 10px;border:1px solid color-mix(in srgb,rgb(var(--accent)) 18%,var(--line));border-radius:calc(5px*var(--rs));') &&
  plannerStyle.includes('.wp-calendar-span{display:flex;align-items:center;gap:8px;min-width:0;min-height:20px;margin:0 8px;padding:1px 8px;') &&
  plannerStyle.includes('.wp-calendar-span--week{height:20px;margin:0 2px;') &&
  plannerStyle.includes('.wp-week-mobile .wp-day-capsule{margin:0 -7px}') &&
  !plannerStyle.includes('.wp-week-mobile .wp-day-capsule{margin-left:-9px') &&
  plannerStyle.includes('.wp-day-items>.wp-done-fold>div{gap:6px}') &&
  plannerStyle.includes('.wp-week-grid.has-spans .wp-day-items{margin-top:6px}'));
check('задачи недели держат единый ритм: зазоры 6px и одинаковый ховер у рядов и капсул',
  plannerStyle.includes('.wp-calendar-span-layer{position:absolute;display:grid;grid-template-columns:repeat(7,minmax(0,1fr));grid-template-rows:repeat(var(--wp-span-lanes),20px);column-gap:10px;row-gap:6px;') &&
  plannerStyle.includes('.wp-day-items{display:flex;flex:1;flex-direction:column;gap:6px;') &&
  plannerStyle.includes('.wp-calendar-span:hover{border-color:rgb(var(--accent));transform:translateY(-1px);box-shadow:0 4px 12px rgb(0 0 0/.16);filter:brightness(1.03);position:relative;z-index:5}') &&
  plannerStyle.includes('.wp-day-capsule:hover{border-color:rgb(var(--accent));transform:translateY(-1px);box-shadow:0 4px 12px rgb(0 0 0/.16);filter:brightness(1.03);position:relative;z-index:5}') &&
  plannerStyle.includes('.wp-day-capsule.dragging{opacity:.45}') &&
  plannerStyle.includes('.wp-calendar-span.dragging{opacity:.45}'));
check('кастомные строки меню держат метрики пунктов: равные отступы сверху и снизу',
  css.includes('.mrow{display:flex;align-items:center;gap:8px;min-height:34px;padding:0 6px 0 10px;'));
check('карточки доски не рвут верстку в узких колонках: мета переносится, чипы режутся многоточием',
  plannerStyle.includes('.wp-board-task .wp-task-meta>span{max-width:100%;min-width:0;overflow:hidden;overflow-wrap:anywhere}') &&
  plannerStyle.includes('.wp-board-task .wp-task-meta .wp-deadline{flex:0 1 auto;min-width:0;white-space:normal}') &&
  plannerStyle.includes('.wp-board-task .wp-chip,.wp-board-task .wp-tag{max-width:100%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}'));
check('статусы красятся из шести свотчей с кольцом активного цвета',
  plannerStyle.includes('.wp-swatches{display:flex;align-items:center;gap:6px;flex:none}') &&
  plannerStyle.includes('.wp-swatch.is-on{box-shadow:0 0 0 2px var(--card),0 0 0 4px var(--sw,var(--line-3))}'));
check('месячная сетка задач повторяет компактные метрики «Истории», без слоя диапазонов — только точки',
  plannerStyle.includes('min-height:84px;aspect-ratio:1.22;') &&
  !plannerStyle.includes('.wp-month-day{height:150px;') &&
  taskLayoutStyle.includes('.planner-page .wp-month-day{height:78px;') &&
  taskLayoutStyle.includes('.planner-page .wp-month-day-head{min-height:24px;gap:3px}') &&
  !plannerStyle.includes('wp-month-span-layer') &&
  !taskLayoutStyle.includes('wp-month-span-layer'));
check('кнопки футера модальных окон центрированы',
  css.includes('.modal-f{display:flex;justify-content:center;gap:8px;'));

// Keep the pre-existing task rail and Pomodoro visual contracts while using the updated release baseline.
check('футер задач прижат к нижней границе и выровнен с пунктами навигации',
  /\.planner-page \.wp-rail\{[^}]*position:sticky;top:0;[^}]*height:100%;[^}]*padding:12px 12px 0;/.test(taskLayoutStyle) &&
  /\.planner-page \.wp-rail-footer\{[^}]*flex-direction:row;[^}]*align-items:center;[^}]*margin-top:auto;[^}]*padding:0 12px;[^}]*font-size:11\.5px/.test(taskLayoutStyle) &&
  taskLayoutStyle.includes('.planner-page .wp-rail-footer span{display:flex;align-items:center;gap:10px;white-space:nowrap}') &&
  taskLayoutStyle.includes('.planner-page .wp-rail-footer .i{width:16px;height:16px;flex:none}') &&
  taskLayoutStyle.includes('.planner-page .wp-rail.is-collapsed{padding:12px 8px 0;') &&
  taskLayoutStyle.includes('width:min(288px,calc(100% - 40px));height:100%;padding:12px 12px 0;'));
check('Pomodoro сохраняет крупное моноширинное кольцо и адаптивные сгруппированные кнопки',
  structureCss.includes('.pomo-ring{width:188px;height:188px}') &&
  structureCss.includes('.pomo-time-wrap b{font:650 42px/1 var(--mono)') &&
  structureCss.includes('.pomo-actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr))') &&
  structureCss.includes('@media(max-width:860px)') && structureCss.includes('@media(max-width:440px)'));
check('Pomodoro popover и уведомление доступны, а perf-анимации отключены',
  structureCss.includes('.pomo-popover[hidden],.pomo-notice[hidden]{display:none!important}') &&
  structureCss.includes('body[data-gfx="perf"] :is(.pomo-global') &&
  html.includes('aria-modal', html.indexOf('function pomoEnsureGlobalLayers()')) &&
  html.includes('data-act="pomoNoticeAck"'));
check('заметка настроек Pomodoro повторяет отступ и цвет .eff-note',
  css.includes('.eff-note{margin-top:12px;padding-top:10px;border-top:1px solid var(--line);color:var(--text-3);font-size:12px;line-height:1.5}') &&
  structureCss.includes('.modal.pomo-settings-modal .modal-b{padding-top:19px}') &&
  structureCss.includes('.modal.pomo-settings-modal .wp-form-hint{margin:12px 0 0;padding-top:10px;border-top:1px solid var(--line);color:var(--text-3);font-size:12px;line-height:1.5}') &&
  html.includes("cls: 'pomo-settings-modal'"));
check('меню виджетов использует штатный переключатель, клавиатурный фокус и локальную настройку',
  css.includes('.wg-picker-menu .mi.wg-toggle-row{gap:11px;min-height:48px}') &&
  css.includes('.wg-picker-menu .mi.wg-toggle-row>.sw-t{margin-left:3px;pointer-events:none}') &&
  css.includes('.wg-picker-menu .mi.wg-toggle-row:focus-visible{outline:2px solid rgb(var(--accent));outline-offset:2px}') &&
  html.includes('data-wg-toggle="${key}" role="switch" aria-checked="${on}"') &&
  html.includes('W[k] = !W[k];') && html.includes("persist();\n    refreshPage('widgets');"));
check('Помодоро не содержит оставшейся визуальной разметки или CSS-индикаторов циклов',
  !css.includes('.pomo-cycle-dots') && !css.includes('.pomo-cycles{') &&
  !html.includes('pomoCycleProgress') && !html.includes('pomoCycleTip') && !html.includes('pomoCycles'));

finish();
