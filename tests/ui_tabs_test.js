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
/* Кусок исходника между двумя якорями (для проверок отдельных функций). */
const cut = (from, to) => {
  const i = html.indexOf(from);
  if (i < 0) return '';
  const j = to ? html.indexOf(to, i) : -1;
  return html.slice(i, j < 0 ? html.length : j);
};
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
check('миниатюра полосы фиксирует узкую ширину в границах стандарта',
  /max-width:40px/.test(pinnedBar) && /min-width:40px/.test(pinnedBar), pinnedBar);
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

// 7. Стандарт вкладок для горизонтальной полосы (сверху/снизу): ширина по
//    умолчанию 200 px, минимум 72 px (диапазон 40–80), высота 32 px
//    (диапазон 32–36), значок сайта и кнопка закрытия 16×16, горизонтальные
//    отступы 10/8 px (диапазон 8–12). Вертикальных панелей это не касается:
//    их ширину задаёт панель, а не стандарт.
const tabStd = rule('.tab-mount .tab');
check('стандарт задан для горизонтальной полосы', tabStd.length > 0, tabStd);
/* Ширину полосы считает layoutTabStrip(): заголовок вкладки на неё не влияет,
   поэтому у всех свободных вкладок полосы одна ширина --tabw. Вне полосы
   переменная не задана, и работает запасное значение 200 px. */
check('ширина вкладки по умолчанию — 200 px',
  /flex:0 1 var\(--tabw,200px\)/.test(tabStd) && /max-width:var\(--tabw,200px\)/.test(tabStd), tabStd);
const minW = +(tabStd.match(/min-width:(\d+)px/) || [])[1];
check('минимальная ширина вкладки в диапазоне 40–80 px', minW >= 40 && minW <= 80, minW + ' px');
const layout = cut('function layoutTabStrip() {', '\nfunction renderTabs()');
check('полоса считает одну ширину на все свободные вкладки',
  /const w = Math\.max\(TAB_MIN, Math\.min\(TAB_MAX, Math\.floor\(\(avail - pin \* TAB_PIN - TAB_GAP \* \(n - 1\)\) \/ free\)\)\)/.test(layout) &&
  /tl\.style\.setProperty\('--tabw', w \+ 'px'\)/.test(layout), layout.length + ' символов');
check('ширина не зависит от текста заголовка',
  layout.length > 0 && !/textContent|scrollWidth|offsetWidth|title/.test(layout));
check('пределы ширины совпадают с CSS: 72 и 200 px',
  /const TAB_MIN = 72, TAB_MAX = 200/.test(html) && minW === 72);
check('панель вне полосы не наследует --tabw',
  /tl\.style\.removeProperty\('--tabw'\)/.test(layout));
check('ряд пересчитывается при изменении окна и списка',
  /new ResizeObserver\(\(\) => layoutTabStrip\(\)\)/.test(html) &&
  /window\.addEventListener\('resize', debounce\(\(\) => \{[^}]*layoutTabStrip\(\)/.test(html) &&
  /layoutTabStrip\(\);/.test(cut('function renderTabs() {', '\nfunction applyTabPos()')));
const stdH = +(tabStd.match(/height:(\d+)px/) || [])[1];
check('высота вкладки в диапазоне 32–36 px', stdH >= 32 && stdH <= 36, stdH + ' px');
const padM = tabStd.match(/padding:0 (\d+)px 0 (\d+)px/);
check('горизонтальные отступы вкладки в диапазоне 8–12 px',
  !!padM && +padM[2] >= 8 && +padM[2] <= 12 && +padM[1] >= 8 && +padM[1] <= 12, padM ? padM[2] + '/' + padM[1] + ' px' : 'нет');
check('значок сайта в полосе — 16×16',
  /^\.tab-mount \.tab \.fav\{width:16px;height:16px/.test(css.split('\n').find(l => l.startsWith('.tab-mount .tab .fav{')) || ''), rule('.tab-mount .tab .fav'));
check('кнопка закрытия в полосе — 16×16',
  /^\.tab-mount \.tab \.x\{width:16px;height:16px/.test(css.split('\n').find(l => l.startsWith('.tab-mount .tab .x{')) || ''), rule('.tab-mount .tab .x'));
check('вертикальная панель не наследует стандарт полосы',
  !/min-width:72px/.test(rule('.tab')) && !/max-width:200px/.test(rule('.tab')), rule('.tab').slice(0, 70));

// 8. Значок сайта — настоящая иконка в слоте значка, а не буква.
const favFn = (html.match(/function favHtml\(t, cls = '', tip = ''\) \{[\s\S]*?\n\}/) || [''])[0];
check('значок сайта берёт иконку из кэша оболочки',
  /FAV_MEM\[fh\]/.test(favFn) && /class="fav fimg/.test(favFn) && /data-favhost/.test(favFn), favFn.length + ' символов');
check('буква остаётся запасным вариантом, когда иконки нет',
  /fav \$\{cls\}/.test(favFn) && /\|\| '•'/.test(favFn));
check('свежая иконка доставляется на место и в бейдж с буквой',
  /:not\(\.fimg\)/.test(cut('function favCacheApply(host) {', '/* Событие') || '') && /classList\.add\('fimg'\)/.test(cut('function favCacheApply(host) {', '/* Событие') || ''));
check('bookmarks и favFor используют общий значок',
  /const favFor = \(url, cls = ''\) => favHtml\(\{ url \}, cls\);/.test(html) &&
  /function bmFavHtml\(b, cls = ''\) \{ return favHtml\(\{ url: b\.url \}, cls, hostOf\(b\.url\)\); \}/.test(html));
check('иконка внутри бейджа: обрезка вместо перекрытия соседних полей',
  /\.fav\.fimg\{position:relative;overflow:hidden\}/.test(css) &&
  /\.fav\.fimg img\{position:absolute;inset:0;width:100%;height:100%;object-fit:cover/.test(css) &&
  !/\.qlink \.fav\.fimg/.test(css));
check('нет глобальных правил для img (размер задаёт только бейдж)',
  !/^img\{/m.test(css) && !/^\s*img\s*,/m.test(css) && /\.fav\.fimg img\{/.test(css));

// 9. Догрузка иконок: у хостов без кэша значок — буква, и её нужно заменить
//    настоящей иконкой. UI не имел своего сетевого пути, поэтому просит
//    оболочку (favicon.ensure) один раз на хост, пачками и с задержкой после
//    монтирования списка.
const favFlush = cut('function favPendingHosts()', 'function bmFavHtml');
check('недостающие иконки собираются с бейджей без картинки',
  /\.fav\[data-favhost\]:not\(\.fimg\)/.test(favFlush) && /favRequested\.has\(h\)/.test(favFlush));
check('хост спрашивается один раз за сессию',
  /const favRequested = new Set\(\)/.test(html) && /batch\.forEach\(h => favRequested\.add\(h\)\)/.test(favFlush));
check('запрос уходит пачками не больше двенадцати хостов',
  /pending\.slice\(0, 12\)/.test(favFlush) && /pending\.length > batch\.length/.test(favFlush));
check('догрузка идёт через оболочку и молчит без неё',
  /window\.shelterCefRequest\('favicon\.ensure', \{ hosts: batch\.join\(' '\) \}\)/.test(favFlush) &&
  /typeof window\.shelterCefRequest !== 'function'\) return;/.test(html) === false &&
  /if \(typeof window\.shelterCefRequest === 'function'\)/.test(favFlush));
check('запрос не привязан к жёсткому таймеру-задержке',
  /favTimer = setTimeout\(favFlush, 120\)/.test(html) && /clearTimeout\(favTimer\)/.test(html));
check('в режиме «Призрак» иконки не запрашиваются',
  /if \(S\.ghost\) return;/.test(favFlush));
const heroLinksFn = cut('function renderHeroLinks()', 'function renderHeroFoot');
check('догрузка запускается после монтирования списков',
  /new MutationObserver\(ms => \{/.test(html) && /ensureFavicons\(\); return;/.test(html) && /ensureFavicons\(\);/.test(heroLinksFn));
check('у моста есть отдельный путь запроса (не легаси-словарь)',
  /window\.shelterCefRequest = \(method, args\) => mq2\(method, args\)/.test(html));

// 10. Арифметика ширины полосы. Прогоняем сам layoutTabStrip() с подставным
//     DOM: одна переменная --tabw на всю полосу, зажим 72…200 px, закреплённые
//     по 40 px, полоса вне горизонтальных доков переменную снимает. Это то
//     требование, которое иначе проверяет только приёмочный прогон в браузере.
const layoutSrc = cut('function layoutTabStrip() {', '\nfunction renderTabs()');
const mkStyle = () => ({ setProperty(k, v) { this[k] = v; }, removeProperty(k) { delete this[k]; } });
const mkCls = () => {
  const set = new Set();
  return { add: c => set.add(c), remove: c => set.delete(c), toggle: (c, on) => { (on ? set.add : set.delete).call(set, c); }, contains: c => set.has(c), size: set.size };
};
const runLayout = opts => {
  const styles = new Map();
  const tabStyle = () => ({ display: 'flex', position: 'relative' });
  const tabs = [];
  for (let i = 0; i < opts.tabs; i++) {
    const cls = mkCls();
    if (i < (opts.pinned || 0)) cls.add('pinned');
    const el = { classList: cls };
    styles.set(el, tabStyle());
    tabs.push(el);
  }
  const list = { style: mkStyle(), classList: mkCls() };
  styles.set(list, tabStyle());
  const siblings = (opts.siblings || []).map(w => {
    const el = { hidden: false, getBoundingClientRect: () => ({ width: w }) };
    styles.set(el, { display: 'flex', position: 'static' });
    return el;
  });
  const mount = {
    id: opts.mount, clientWidth: opts.width, hidden: false,
    children: [list].concat(siblings), getBoundingClientRect: () => ({ width: opts.width })
  };
  styles.set(mount, {
    display: 'flex', position: 'relative', columnGap: (opts.gap || 0) + 'px', gap: (opts.gap || 0) + 'px',
    paddingLeft: (opts.padL || 0) + 'px', paddingRight: (opts.padR || 0) + 'px'
  });
  list.parentElement = mount;
  const getComputedStyle = el => styles.get(el) || { display: 'block', position: 'static' };
  const make = new Function(
    'TAB_MIN', 'TAB_MAX', 'TAB_GAP', 'TAB_PIN', '$', '$$', 'getComputedStyle',
    layoutSrc + '\nreturn layoutTabStrip;'
  );
  const layout = make(72, 200, 4, 40, () => list, () => tabs, getComputedStyle);
  layout();
  return { width: list.style['--tabw'], tight: list.classList.contains('tight'), hasVar: '--tabw' in list.style, tabs: tabs.length };
};
check('широкая полоса: ширина упирается в максимум 200 px',
  runLayout({ mount: 'tabTop', width: 1200, padL: 10, padR: 10, gap: 4, siblings: [30], tabs: 6, pinned: 2 }).width === '200px',
  JSON.stringify(runLayout({ mount: 'tabTop', width: 1200, padL: 10, padR: 10, gap: 4, siblings: [30], tabs: 6, pinned: 2 })));
const mid = runLayout({ mount: 'tabTop', width: 700, padL: 10, padR: 10, gap: 4, siblings: [30], tabs: 6, pinned: 2 });
check('средняя полоса: ширина считается от свободного места', mid.width === '136px', JSON.stringify(mid));
const narrow = runLayout({ mount: 'tabBottom', width: 500, padL: 10, padR: 10, gap: 4, siblings: [30], tabs: 6, pinned: 2 });
check('узкая полоса: вкладки сжимаются, но не ниже 72 px', narrow.width === '86px' && narrow.tight === true, JSON.stringify(narrow));
const floor = runLayout({ mount: 'tabTop', width: 400, padL: 10, padR: 10, gap: 4, siblings: [30], tabs: 6, pinned: 2 });
check('предельно узкая полоса: ширина остаётся на минимуме 72 px', floor.width === '72px', JSON.stringify(floor));
const free = runLayout({ mount: 'tabTop', width: 600, padL: 8, padR: 8, gap: 4, siblings: [28], tabs: 3 });
check('без закреплённых ширина делится на все вкладки',
  free.width === '181px' && free.tabs === 3, JSON.stringify(free));
const noRoom = runLayout({ mount: 'tabTop', width: 50, padL: 10, padR: 10, gap: 4, siblings: [30], tabs: 4, pinned: 1 });
check('когда места нет вовсе, переменная не выставляется', noRoom.hasVar === false, JSON.stringify(noRoom));
const vertical = runLayout({ mount: 'dockLeft', width: 900, padL: 8, padR: 8, gap: 4, siblings: [], tabs: 5 });
check('вертикальная панель снимает переменную полосы', vertical.hasVar === false, JSON.stringify(vertical));

const failed = results.filter(x => !x).length;
console.log(failed ? 'UI_TABS_TEST_FAIL ' + failed : 'UI_TABS_TEST_PASS ' + results.length + '/' + results.length);
process.exit(failed ? 1 : 0);
