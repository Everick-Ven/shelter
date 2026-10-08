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

const styleMatch = html.match(/<style[^>]*>([\s\S]*?)<\/style>/);
if (!styleMatch) fail('таблица стилей найдена в index.html');
const css = styleMatch[1];
check('таблица стилей найдена в index.html', true, css.length + ' символов');

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

finish();
