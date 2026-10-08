// Регрессия кнопки «Новая вкладка» и закреплённых вкладок.
//
// Что стережём:
//   1) кнопка «+» идёт сразу за вкладками: контейнер полосы вкладок не
//      растягивается на всю ширину (иначе «+», вставляемый сразу после
//      контейнера, уезжает к правому краю — так и случилось в 43009c1);
//   2) закреплённая вкладка — миниатюра только в горизонтальной полосе
//      (значок сайта без заголовка); в вертикальной панели закрепление
//      вкладку не сжимает и заголовок остаётся;
//   3) миниатюра задана только для полосы сверху/снизу;
//   4) закреплённые вкладки идут первыми в списке;
//   5) в полосе сверху/снизу «+» вставляется сразу после списка вкладок;
//   6) значок миниатюры остаётся по центру и в свёрнутой панели: правила
//      `.app.collapsed .tab` / `#tabRight.is-collapsed .tab` задают padding-left
//      и по специфичности перебивали миниатюру (значок уезжал на 5–8 px).
//
// Тест читает resources/ui/index.html как текст: без браузера, без зависимостей.
'use strict';
const fs = require('node:fs');
const path = require('node:path');

const file = process.env.SHELTER_UI_FILE || path.join(__dirname, '..', 'resources', 'ui', 'index.html');
const html = fs.readFileSync(file, 'utf8');

const results = [];
const check = (name, ok, detail) => {
  results.push(!!ok);
  console.log((ok ? 'PASS ' : 'FAIL ') + name + (detail !== undefined ? ' :: ' + detail : ''));
};

const css = (html.match(/<style[^>]*>([\s\S]*?)<\/style>/) || [])[1] || '';
/* Правило ищем по строкам: в этом файле каждое правило занимает свою строку,
   а перед некоторыми стоят комментарии — привязка к «}» была бы хрупкой. */
const rule = sel => {
  const line = css.split('\n').find(l => l.startsWith(sel + '{'));
  return line ? line.slice(sel.length + 1, -1) : '';
};

// 1. Полоса вкладок по размеру вкладок — «+» держится рядом.
const tabsRule = rule('.tab-mount .tabs');
check('правило полосы вкладок найдено', tabsRule.length > 0, tabsRule.slice(0, 80));
check('полоса вкладок не растягивается на всю ширину (flex:0 1 auto)',
  /flex:0 1 auto/.test(tabsRule) && !/flex:1 1 auto/.test(tabsRule));
check('полоса вкладок остаётся прокручиваемой при переполнении', /overflow-x:auto/.test(tabsRule));

// 2. Компактная миниатюра — только у горизонтальной полосы; в вертикальной
//    панели закрепление вкладку не сжимает (правило .tab.pinned с шириной
//    38 px удалено: оно давало 38-пиксельный «квадрат» в списке слева/справа).
const pinnedSide = rule('.tab.pinned');
const pinnedBar = rule('.tab-mount .tab.pinned');
check('общего сжатия .tab.pinned больше нет', pinnedSide.length === 0, pinnedSide || 'нет правила — верно');
check('миниатюра закреплённой вкладки для полосы сверху/снизу задана', pinnedBar.length > 0, pinnedBar);
check('миниатюра полосы фиксирует узкую ширину',
  /max-width:38px/.test(pinnedBar) && /min-width:38px/.test(pinnedBar), pinnedBar);
check('миниатюра полосы центрирует значок и убирает отступы',
  /justify-content:center/.test(pinnedBar) && /padding:0\b/.test(pinnedBar) && /gap:0\b/.test(pinnedBar), pinnedBar);

// 3. Разметка: вид закреплённой вкладки зависит от ориентации панели.
const render = (html.match(/function renderTabs\(\)[\s\S]*?\n\}/) || [''])[0];
check('рендер списка вкладок найден', render.length > 0, render.length + ' символов');
check('закреплённая вкладка получает класс pinned', /t\.pinned \? ' pinned' : ''/.test(render));
check('ориентация полосы определяется один раз', /const classic = S\.ui\.tabPos === 'top' \|\| S\.ui\.tabPos === 'bottom'/.test(render));
check('миниатюра только для закреплённых в горизонтальной полосе',
  /const mini = t => t\.pinned && classic;/.test(render));
check('в вертикальной панели у закреплённой вкладки есть заголовок',
  /mini\(t\) \? '' : `<span class="t hc">/.test(render));
check('у закреплённой вкладки не рисуется крестик закрытия',
  /t\.pinned \? '' : `<button class="x hc"/.test(render));
check('метка закрепления из разметки убрана', !/ico\('pin', 'pin hc'\)/.test(render));
check('подсказка закреплённой вкладки содержит заголовок',
  /data-tip="\$\{esc\(t\.title\)\}"/.test(render));

// 4. Закреплённые вкладки идут первыми.
check('закреплённые вкладки сортируются в начало списка',
  /filter\(t => t\.pinned\)\.concat\(sp\.tabs\.filter\(t => !t\.pinned\)\)/.test(render));

// 5. «+» вставляется сразу после списка вкладок.
check('кнопка «+» вставляется сразу после списка вкладок', /tl\.after\(plus\)/.test(render));
check('кнопка «+» существует в разметке боковых доков',
  /data-act="newTab" aria-label="Новая вкладка"/.test(html));

// 6. Свёрнутые контексты: миниатюра остаётся центрированной в горизонтальном
//    доке; в вертикальной панели закреплённая вкладка — обычная и следует
//    общим правилам свёртывания (её значок стоит там же, где у соседей).
const cpAt = css.indexOf('.app.collapsed .tab-mount .tab.pinned');
const collapsedPin = cpAt < 0 ? '' : css.slice(cpAt, css.indexOf('}', cpAt));
check('миниатюра полосы центрируется и при свёрнутой панели',
  cpAt >= 0 && /padding:0/.test(collapsedPin) && /justify-content:center/.test(collapsedPin), collapsedPin.slice(0, 90));
check('вертикальной миниатюры в свёрнутых контекстах нет',
  !/\.app\.collapsed \.tab\.pinned/.test(css) && !/#tabRight\.is-collapsed \.tab\.pinned/.test(css));

const failed = results.filter(x => !x).length;
console.log(failed ? 'UI_TABS_TEST_FAIL ' + failed : 'UI_TABS_TEST_PASS ' + results.length + '/' + results.length);
process.exit(failed ? 1 : 0);
