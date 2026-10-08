// Регрессия панели настроек и тулбара.
//
// Что стережём (каждое — уже случавшаяся или почти случившаяся поломка):
//   1) иконка автосогласия cookies живёт в тулбаре рядом с остальными
//      защитными переключателями — кнопку нельзя потерять при правках
//      разметки тулбара;
//   2) в списке настроек строка «Автосогласие cookies» идёт без иконки —
//      переключатель остаётся, значок убираем;
//   3) переход по разделам настроек мгновенный: плавная прокрутка на пять
//      экранов длится около полутора секунд и читается как «настройки
//      грузятся», поэтому `scroll-behavior:smooth` в панели и `behavior:
//      'smooth'` в переходах недопустимы;
//   4) панель остаётся ленивой ровно в одном смысле — она не подменяет
//      отрисовку секций оценкой высот (content-visibility ломает точность
//      offsetTop, а выигрыша не даёт — замерено);
//   5) сброс поиска возвращает весь список разделов, а не прячет те, у которых
//      нет обычных строк (у «О SHELTER» их нет).
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

const cut = (from, to) => {
  const i = html.indexOf(from);
  if (i < 0) return '';
  const j = to ? html.indexOf(to, i) : -1;
  return html.slice(i, j < 0 ? html.length : j);
};

// 1. Тулбар: список плиток защиты.
const tiles = cut('const tiles = [', '];\n  const g = $(\'#ctlGrid\')');
check('тулбар защитных переключателей найден', tiles.length > 0, tiles.length + ' символов');
for (const key of ['ghost', 'trackers', 'https', 'fp', 'cookies']) {
  check('в тулбаре есть переключатель «' + key + '»', new RegExp("\\['" + key + "', '").test(tiles));
}
check('плитка cookies замыкает ряд защитных переключателей, огонь — после неё',
  tiles.indexOf("['cookies', 'cookie',") > tiles.indexOf("['fp', 'finger',") &&
  tiles.indexOf("['cookies', 'cookie',") < cut('const tiles = [', '];\n  const g = $(\'#ctlGrid\')').length);
check('у плитки cookies задана подсказка про автосогласие',
  /'cookies', 'cookie', 'Автосогласие cookies/.test(tiles));

// 2. Строка в настройках — без иконки, с тумблером.
const settings = cut('function openSettings(sec) {', 'function applyFontScale()');
check('панель настроек найдена', settings.length > 0, settings.length + ' символов');
check('строка «Автосогласие cookies» без иконки',
  /row\('Автосогласие cookies'/.test(settings) && !/row\(ico\('cookie'\) \+ 'Автосогласие cookies'/.test(settings));
check('у строки «Автосогласие cookies» остался тумблер', /row\('Автосогласие cookies'[^\n]*sw\('cookies'\)/.test(settings));

// 3. Прокрутка панели без плавной анимации.
const css = (html.match(/<style[^>]*>([\s\S]*?)<\/style>/) || [])[1] || '';
const setBody = (css.match(/\.set-body\{[^}]*\}/) || [''])[0];
check('правило .set-body найдено', setBody.length > 0, setBody.length + ' символов');
check('панель настроек прокручивается без scroll-behavior:smooth', !/scroll-behavior\s*:\s*smooth/.test(setBody));
check('переход по разделу без плавной анимации', !/scrollTo\(\{[^}]*behavior\s*:\s*(REDUCED\(\)\s*\?\s*'auto'\s*:\s*)?'smooth'/.test(settings) &&
  !/scrollTo\(\{[^}]*behavior\s*:\s*/.test(cut("if (nb) {", 'const b = e.target.closest')));
check('переход по разделу идёт прыжком на 18 px', /body\.scrollTo\(\{ top: s\.offsetTop - 18 \}\)/.test(settings));
check('начальный раздел открывается тем же прыжком', /setTimeout\(\(\) => \{ body\.scrollTo\(\{ top: s\.offsetTop - 18 \}\); syncNav\(\); \}, 40\)/.test(settings));

// 4. Никакой подмены высот секций оценкой.
check('секции настроек не подменяются оценкой высоты (content-visibility)', !/content-visibility/.test(css));

// 4b. Телефонный режим: ряд действий переносится, поэтому шестая плитка
//     требует поджатых промежутков — иначе «Настройки» и «Меню» разъезжаются
//     по разным строкам (это ловит приёмочный тест на 390 px).
const phone = cut('@media (max-width:720px){\n  .sb{position:fixed', '@container shelter (max-width:1100px)');
check('телефонный блок найден', phone.length > 0, phone.length + ' символов');
check('в телефонном режиме плитки защиты поджаты', /\.tb-ctls \.ctl\{width:2[0-9]px\}/.test(phone));
check('в телефонном режиме нет своего отступа перед быстрыми действиями',
  !/\.tb-actions>#quickBtn\{margin-left:(?!16px)/.test(phone));

// 4c. Отложенная часть переключения графического режима обязана иметь
//     страховку таймером: на удалённом раннере кадры могут не приходить
//     вовсе, и тогда режим остаётся применённым наполовину (reduce-motion
//     применён, glow-off — нет) — это роняло приёмочный тест на macOS.
check('отложенная часть режима страхуется таймером', /requestAnimationFrame\(run\);/.test(html) && /setTimeout\(run, 50\);/.test(html));

// 5. Поиск: сброс показывает все разделы.
check('сброс поиска не прячет разделы без обычных строк', /let any = !q;/.test(settings));
check('подсветка раздела синхронизируется сразу после прыжка', /body\.scrollTo\(\{ top: s\.offsetTop - 18 \}\); syncNav\(\)/.test(settings));

const failed = results.filter(x => !x).length;
console.log(failed ? 'UI_SETTINGS_TEST_FAIL ' + failed : 'UI_SETTINGS_TEST_PASS ' + results.length + '/' + results.length);
process.exit(failed ? 1 : 0);
