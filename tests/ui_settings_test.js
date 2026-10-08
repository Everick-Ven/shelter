// Регрессия панели настроек и тулбара.
//
// Что стережём (каждое — уже случавшаяся или почти случившаяся поломка):
//   1) все переключатели защит (включая автосогласие cookies) живут в попапе
//      иконки щита в тулбаре — при правках разметки нельзя потерять ни саму
//      иконку, ни одну из строк PROT_ROWS, ни плитку «Удалить сессии»;
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
//      нет обычных строк (у «О SHELTER» их нет);
//   6) ползунок «Скругления углов» тянет за собой весь интерфейс: точечные
//      радиусы заданы через множитель --rs (иначе скругления менялись только
//      у карточек-токенов, а тулбар/меню/поля оставались прежними);
//   7) открытие окна не тормозит: позиции сегментов читаются пакетно (девять
//      «запись → чтение» подряд давали ~70 мс принудительных reflow на первом
//      открытии), поиск фильтрует с задержкой 150 мс по заранее прочитанному
//      тексту, а масштаб шрифта не пишет те же значения повторно.
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

// 1. Тулбар: плитки-переключатели защит переехали в попап щита (#protBtn),
//    в тулбаре остаётся только плитка «Удалить сессии». Проверяем, что ни один
//    переключатель не потерялся: попап щита обязан собрать все строки PROT_ROWS.
const grid = cut("const g = $('#ctlGrid')", 'function themeSwatchesMarkup');
check('плитка «Удалить сессии» рисуется в тулбаре', /class="ctl fire"/.test(grid) && /data-act="fire"/.test(grid), grid.length + ' символов');
check('в тулбаре не осталось плиток-переключателей защит', grid.length > 0 && !/data-sw/.test(grid) && !/const tiles = \[/.test(html));
const shieldBtn = cut('<button class="tb-btn" id="protBtn"', '</button>');
check('иконка щита в тулбаре открывает попап защит', shieldBtn.length > 0 && /data-act="prot"/.test(shieldBtn));
const pop = cut('function protPop(anchor) {', '/* «Расширения»');
check('попап щита собирает строки защит из PROT_ROWS', pop.length > 0 && /PROT_ROWS\.map/.test(pop), pop.length + ' символов');
const protRows = cut('const PROT_ROWS = [', '\nfunction protHtml');
for (const key of ['trackers', 'cookies', 'https', 'fp', 'strict', 'ghost']) {
  check('в попапе щита есть переключатель «' + key + '»', new RegExp("\\['" + key + "', '").test(protRows));
}
check('у строки cookies задана подсказка про автосогласие',
  /'cookies', 'cookie', 'Автосогласие cookies/.test(protRows));

// 2. Строка в настройках — без иконки, с тумблером.
/* Разметку окна собирает settingsHtml(), поведение окна осталось в openSettings():
   проверки ниже смотрят каждая в свой кусок. */
const settingsMarkup = cut('function settingsHtml() {', 'function prewarmSettings()');
const settings = cut('function openSettings(sec) {', 'function applyFontScale()');
check('панель настроек найдена', settings.length > 0, settings.length + ' символов');
check('разметка настроек собирается сборщиком', settingsMarkup.length > 0, settingsMarkup.length + ' символов');
check('строка «Автосогласие cookies» без иконки',
  /row\('Автосогласие cookies'/.test(settingsMarkup) && !/row\(ico\('cookie'\) \+ 'Автосогласие cookies'/.test(settingsMarkup));
check('у строки «Автосогласие cookies» остался тумблер', /row\('Автосогласие cookies'[^\n]*sw\('cookies'\)/.test(settingsMarkup));

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

// 6. Скругления: один множитель на весь интерфейс.
check('множитель скруглений объявлен по умолчанию', /--rs:\s*1;/.test(html));
const radiusFn = cut('function applyRadius() {', '\nfunction ');
check('ползунок задаёт множитель вместе с токенами',
  ['--rs', '--r-m', '--r-l', '--r-xl', '--r-s', '--r-xs'].every(t => radiusFn.includes("'" + t + "'")), radiusFn.length + ' символов');
const radii = [...html.matchAll(/border-radius:([^;}"]+)/g)].map(m => m[1].trim());
const literal = radii.filter(v => /^\d+(\.\d+)?px$/.test(v) && v !== '99px');
check('литеральные радиусы следуют за множителем', literal.length === 0, literal.join(', ') || 'нет');
check('пилюли и круги остались полностью скруглёнными',
  radii.includes('99px') && radii.includes('50%'));

// 7. Производительность открытия и поиска.
const segAll = cut('function segSyncAll(', '\nfunction initSegs');
check('пакетная синхронизация сегментов есть', segAll.length > 0, segAll.length + ' символов');
check('геометрия сегментов читается до записей',
  segAll.indexOf('offsetLeft') >= 0 && segAll.indexOf('offsetLeft') < segAll.indexOf('setProperty'));
check('initSegs использует пакетный путь', /function initSegs\(root = document\) \{ segSyncAll\(/.test(html));
const search = cut('const searchIndex = $$', 'const setShown');
check('поисковый индекс строится заранее', search.length > 0 && /r\.t\.includes\(q\)/.test(html));
const runSearch = cut('const runSearch = value => {', "$('#setQ', root).addEventListener");
check('поиск применяется с задержкой 120–180 мс', /\}, 1[2-8][0-9]\);/.test(runSearch), (runSearch.match(/\}, (\d+)\);/) || [])[1] + ' мс');
check('устаревший результат отбрасывается по номеру запроса', /seq !== searchSeq/.test(runSearch));
check('очистка поля фильтрует сразу, без задержки', /if \(!q\) \{ clearTimeout\(searchTimer\); searchTimer = 0; searchSeq\+\+; filterSettings\(''\); return; \}/.test(runSearch));
check('display меняется только при отличии', /if \(el\.style\.display !== v\) el\.style\.display = v;/.test(html));
const fontFn = cut('function applyFontScale()', '\nfunction installFontScale');
check('масштаб шрифта не пишет те же значения', /const setVar = \(k, v\) => \{ if \(root\.getPropertyValue\(k\) !== v\) root\.setProperty\(k, v\); \}/.test(fontFn));
check('масштаб шрифта не переписывает подписи без изменений', /if \(l && l\.textContent !== v \+ ' %'\) l\.textContent/.test(fontFn));
const sync = cut('function syncSwitches()', 'const PREF_TOAST');
check('синхронизация тумблеров не пишет совпадающие состояния',
  /if \(b\.classList\.contains\('on'\) !== on\)/.test(sync) && /if \(b\.getAttribute\('aria-checked'\) !== sa\)/.test(sync));

// 7. Прогрев кешей окна настроек (ТЗ №2). Разметка строится тем же сборщиком
//    один раз в скрытом контейнере вне потока, контейнер удаляется в той же
//    синхронной задаче, DOM модалки по-прежнему создаётся только при показе.
//    Прогрев уходит в простой и не использует таймеров-задержек.
const pw = cut('function prewarmSettings()', 'function openSettings(sec) {');
check('окно и прогрев собираются одним сборщиком разметки',
  /box\.innerHTML = settingsHtml\(\)/.test(pw) && /function settingsHtml\(\)/.test(html) && /return html;/.test(html));
check('контейнер прогрева скрыт, вне потока и всегда удаляется',
  /id = 'setPrewarm'/.test(pw) && /visibility:hidden/.test(pw) && /contain:strict/.test(pw) && /finally \{ if \(box\) box\.remove\(\); \}/.test(pw));
check('прогрев одноразовый и уступает уже открытому окну',
  /if \(settingsPrewarmDone\) return;/.test(pw) && /if \(\$\('\.settings'\)\) \{ settingsPrewarmDone = true; return; \}/.test(pw));
check('флаг прогрева ставится только после успеха',
  /settingsPrewarmDone = true;\n  \} catch/.test(pw));
check('прогрев запускается в простое и по наведению на кнопку настроек',
  /requestIdleCallback/.test(html) && /warmSettings, \{ timeout: 2000 \}/.test(html) &&
  /setBtn\.addEventListener\('pointerenter', warmSettings/.test(html) && /setBtn\.addEventListener\('focus', warmSettings/.test(html));
check('в прогреве нет таймеров-задержек', !/setTimeout/.test(pw));
check('окно настроек берёт общую разметку, а не строит её заново',
  /const html = settingsHtml\(\);/.test(settings));

const failed = results.filter(x => !x).length;
console.log(failed ? 'UI_SETTINGS_TEST_FAIL ' + failed : 'UI_SETTINGS_TEST_PASS ' + results.length + '/' + results.length);
process.exit(failed ? 1 : 0);
