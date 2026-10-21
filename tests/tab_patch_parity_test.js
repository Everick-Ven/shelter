'use strict';

// P1-5: точечный патч вкладки в renderTabs. Обновление заголовка/иконки/
// состояния одной вкладки не должно пересобирать весь список.
//
// Что стережём:
//   1) у renderTabs один общий шаблон htmlOf — патч и полный рендер рисуют
//      одну и ту же разметку (классы .tab/.active/.pinned/.enter, data-act,
//      data-tab, draggable, data-tip, значок, заголовок, крестик);
//   2) при совпадении состава и порядка id переписываются только
//      изменившиеся узлы (по data-tab); добавление/удаление/перестановка и
//      enter-анимация — полный рендер;
//   3) DOM вкладок после патча идентичен DOM после полного рендера
//      (сравнение outerHTML в пробнике);
//   4) хвост renderTabs сохранён: layoutTabStrip, tabsScrollSync,
//      positionNavInd, syncQuickPanel, кнопка «+».
//
// Пробник — мини-DOM без зависимостей: outerHTML узла равен строке, которую
// записал рендер; сравнение строк = сравнение outerHTML.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const results = [];
const check = (name, ok, detail) => {
  results.push(!!ok);
  console.log((ok ? 'PASS ' : 'FAIL ') + name + (detail !== undefined ? ' :: ' + detail : ''));
};

const file = process.env.SHELTER_UI_FILE || path.join(__dirname, '..', 'resources', 'ui', 'index.html');
const html = fs.readFileSync(file, 'utf8');

// ---- 1. Статистические пины. ----
const render = (html.match(/function renderTabs\(\)[\s\S]*?\n\}/) || [''])[0];
check('renderTabs найден', render.length > 0, render.length + ' символов');
check('шаблон вкладки вынесен в общий htmlOf',
  /const htmlOf = t =>/.test(render) && !/tabs\.map\(t =>/.test(render));
check('точечный патч сравнивает состав и порядок id',
  /sameList/.test(render) && /nodes\[i\]\.dataset\.tab === t\.id/.test(render));
check('патч переписывает только изменившийся узел',
  /nodes\[i\]\.outerHTML = htmls\[i\]/.test(render) && /tHtml\.set\(t\.id, htmls\[i\]\)/.test(render));
check('полный рендер остаётся для структуры (join + карта шаблонов)',
  /tl\.innerHTML = htmls\.join\(''\)/.test(render) &&
  /renderTabs\._html = new Map\(tabs\.map\(\(t, i\) => \[t\.id, htmls\[i\]\]\)\)/.test(render));
check('enter-анимация просит полный рендер (justAdded)',
  /!justAdded &&/.test(render) && /t\.id === justAdded \? ' enter' : ''/.test(render));
check('классы .tab/.active/.pinned/.enter не менялись',
  /class="tab\$\{t\.id === S\.activeTabId \? ' active' : ''\}\$\{t\.pinned \? ' pinned' : ''\}\$\{t\.id === justAdded \? ' enter' : ''\}"/.test(render));
check('conditional draggable сохранён (data-act/data-tab/data-tip тоже)',
  /draggable="\$\{S\.prefs\.dragTabs \? 'true' : 'false'\}"/.test(render) &&
  /data-act="tab"/.test(render) && /data-tab="\$\{t\.id\}"/.test(render) &&
  /data-tip="\$\{esc\(t\.title\)\}"/.test(render));
check('хвост renderTabs сохранён (layout/scroll/индекс/панель, «+» после списка)',
  /layoutTabStrip\(\);/.test(render) && /tabsScrollSync\(\);/.test(render) &&
  /positionNavInd\(\);/.test(render) && /syncQuickPanel\(\);/.test(render) &&
  /tl\.after\(plus\)/.test(render));

// ---- 2. Пробник: патч == полный рендер (outerHTML), точечность, структура. ----
function makeList() {
  const tl = {
    _nodes: [],
    _tail: '',
    querySelectorAll() { return this._nodes.slice(); },
    querySelector() { return null; },
    after() {},
    scrollLeft: 0,
    clientWidth: 0,
  };
  Object.defineProperty(tl, 'innerHTML', {
    get() { return this._nodes.map(n => n._html).join('') + this._tail; },
    set(v) {
      /* Одна вкладка = один <div class="tab ...>...</div>; «+» — хвост. */
      const re = /<div class="tab[\s\S]*?<\/div>/g;
      const chunks = v.match(re) || [];
      this._tail = v.replace(re, '');
      this._nodes = chunks.map(chunk => {
        const id = (chunk.match(/data-tab="([^"]*)"/) || [])[1];
        return {
          dataset: { tab: id },
          _html: chunk,
          writes: 0,
          get outerHTML() { return this._html; },
          set outerHTML(x) { this._html = x; this.writes++; },
        };
      });
    },
  });
  return tl;
}

function makeWorld() {
  const tl = makeList();
  const state = {
    ui: { tabPos: 'left' },
    prefs: { dragTabs: false },
    activeTabId: 'b',
    tabs: [
      { id: 'a', title: 'А', url: 'https://a/', pinned: false },
      { id: 'b', title: 'Б', url: 'https://b/', pinned: false },
      { id: 'c', title: 'В', url: 'https://c/', pinned: false },
    ],
    tail: { layout: 0, scroll: 0, nav: 0, quick: 0 },
  };
  const sandbox = {
    S: state,
    curSpace: () => ({ tabs: state.tabs }),
    justAdded: null,
    esc: s => String(s == null ? '' : s),
    favHtml: t => `<span class="fav">${String(t.title).slice(0, 1)}</span>`,
    ico: () => '<i></i>',
    $: sel => (sel === '#tabList' ? tl : null),
    $$: () => [],
    document: {
      querySelectorAll: () => [],
      createElement: () => ({ classList: { add() {} }, setAttribute() {}, remove: () => {} }),
    },
    layoutTabStrip: () => { state.tail.layout++; },
    tabsScrollSync: () => { state.tail.scroll++; },
    positionNavInd: () => { state.tail.nav++; },
    syncQuickPanel: () => { state.tail.quick++; },
  };
  vm.runInNewContext(
    render + '\n;this.__renderTabs = renderTabs;', sandbox, { filename: 'renderTabs.js' });
  return { tl, state, sandbox, renderTabs: sandbox.__renderTabs };
}

// Сценарий A: патч — правка заголовка, активной вкладки и ghost-флага.
const w1 = makeWorld();
w1.renderTabs(); // первый проход — полный рендер
const clean1 = w1.tl._nodes.map(n => n.outerHTML);
w1.state.tabs[0].title = 'А2';
w1.state.tabs[2].ghost = true;
w1.state.activeTabId = 'c';
w1.renderTabs(); // тот же состав и порядок -> патч
const patched = w1.tl._nodes.map(n => n.outerHTML);

// Сценарий B: тот же стейт, но с нуля — полный рендер.
const w2 = makeWorld();
w2.state.tabs[0].title = 'А2';
w2.state.tabs[2].ghost = true;
w2.state.activeTabId = 'c';
w2.renderTabs();
const full = w2.tl._nodes.map(n => n.outerHTML);

check('DOM вкладок после патча идентичен DOM после полного рендера (outerHTML)',
  JSON.stringify(patched) === JSON.stringify(full),
  patched.join('').slice(0, 120));
check('хвосты renderTabs идут и при патче (layout/scroll/индекс/панель)',
  w1.state.tail.layout > 1 && w1.state.tail.scroll > 1 &&
  w1.state.tail.nav > 1 && w1.state.tail.quick > 1,
  JSON.stringify(w1.state.tail));

// Точечность: правка заголовка второй вкладки переписывает только её.
const w3 = makeWorld();
w3.renderTabs();
w3.tl._nodes.forEach(n => { n.writes = 0; });
w3.state.tabs[1].title = 'Б2';
w3.renderTabs();
const writes = w3.tl._nodes.map(n => n.writes);
check('патч переписывает только изменившийся узел (2 нуля, 1 перезапись)',
  writes[0] === 0 && writes[1] === 1 && writes[2] === 0, 'writes=' + writes.join(','));
check('неизменённый узел сохраняет прежний outerHTML',
  w3.tl._nodes[0].outerHTML === clean1[0] && w3.tl._nodes[2].outerHTML === clean1[2]);

// Структура: добавление -> полный рендер с enter-анимацией.
const w4 = makeWorld();
w4.renderTabs();
w4.state.tabs.push({ id: 'd', title: 'Г', url: 'https://d/', pinned: false });
w4.sandbox.justAdded = 'd';
w4.renderTabs();
const htmls4 = w4.tl._nodes.map(n => n.outerHTML);
check('добавление вкладки идёт полным рендером (enter у новой)',
  htmls4.length === 4 && /class="tab[^"]* enter"/.test(htmls4[3]) && !/enter/.test(htmls4[0]),
  htmls4[3].slice(0, 80));
check('enter-анимация сбрасывается (justAdded обнуляется)', w4.sandbox.justAdded === null);

// Перестановка -> полный рендер (состав и порядок id не совпали).
const w5 = makeWorld();
w5.renderTabs();
w5.state.tabs = [w5.state.tabs[1], w5.state.tabs[0], w5.state.tabs[2]];
w5.renderTabs();
const w6 = makeWorld();
w6.state.tabs = [w6.state.tabs[1], w6.state.tabs[0], w6.state.tabs[2]];
w6.renderTabs();
check('перестановка вкладок идёт полным рендером (порядок b,a,c, outerHTML == полный рендер)',
  w5.tl._nodes.map(n => n.dataset.tab).join(',') === 'b,a,c' &&
  JSON.stringify(w5.tl._nodes.map(n => n.outerHTML)) === JSON.stringify(w6.tl._nodes.map(n => n.outerHTML)));

if (results.includes(false)) {
  console.error('tab_patch_parity_test: FAIL');
  process.exit(1);
}
console.log('tab_patch_parity_test: OK');
