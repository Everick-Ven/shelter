'use strict';

// P0-3: наблюдатели всего DOM (favicon-наблюдатель и патч помощника) не
// должны гонять полные сканирования на каждую мутацию — особенно на стриме
// ответа, где узлы добавляются каждое обновление.
//
// Что стережём:
//   1) favicon-наблюдатель дёшево проверяет только добавленные узлы
//      (сам узел — `.fav`, глубже — только по живым потомкам) и группирует
//      ensureFavicons() одним вызовом на кадр (rAF + favsQueued);
//   2) наблюдатель помощника (patchAssistant) запускается только когда
//      добавленное касается патченых узлов (DT_PATCH_SEL: сам .dt-app,
//      шапка/док или поле ввода), а не на каждое сообщение стрима;
//   3) актуальный контракт: privacy-pill/mac-send не создаются, кнопка
//      отправки остаётся в нижнем ряду, «Новый чат» и macGrow сохранены;
//   4) замер до/после: код наблюдателей до оптимизации (замороженный baseline
//      из коммита b5347e1) и из рабочей копии гоняется в одном пробнике на
//      одинаковом потоке мутаций.
//
// Тест читает resources/ui/index.html как текст и исполняет только блоки
// наблюдателей: без браузера, без зависимостей.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const results = [];
const check = (name, ok, detail) => {
  results.push(!!ok);
  console.log((ok ? 'PASS ' : 'FAIL ') + name + (detail !== undefined ? ' :: ' + detail : ''));
};

const file = process.env.SHELTER_UI_FILE || path.join(__dirname, '..', 'resources', 'ui', 'index.html');
const html = fs.readFileSync(file, 'utf8');
// Замороженный baseline «до оптимизации» (коммит b5347e1): сравниваем с ним,
// а не с git HEAD — после закрепления оптимизации в истории контекст не тает.
const BASELINE_FAVICON_OBSERVER = `new MutationObserver(ms => {
      for (const m of ms) for (const n of m.addedNodes) {
        if (n.nodeType === 1 && (n.classList.contains('fav') || n.querySelector('.fav[data-favhost]'))) { ensureFavicons(); return; }
      }
    }).observe(document.body, { childList: true, subtree: true });`;
const BASELINE_ASSISTANT_OBSERVER = 'new MutationObserver(schedule).observe(document.documentElement, { childList: true, subtree: true });';

// ---- 1. Статистические пины: дешёвые ворота и группировка на кадр. ----
check('favicon: узел-значок распознаётся по .fav', /n\.matches\('\.fav'\)/.test(html));
check('favicon: глубже ищется только по живым потомкам',
  /n\.firstChild && n\.querySelector\('\.fav\[data-favhost\]'\)/.test(html));
check('favicon: ensureFavicons один на кадр (favsQueued + rAF)',
  /favsQueued = true;\s*requestAnimationFrame\(\(\) => \{ favsQueued = false; ensureFavicons\(\); \}\)/.test(html));
check('помощник: наблюдатель смотрит только добавленные узлы (addedNodes)',
  /new MutationObserver\(ms => \{[\s\S]*?for \(const n of m\.addedNodes\)[\s\S]*?needsAssistantPatch\(n\)/.test(html));

const dtSel = (html.match(/const DT_PATCH_SEL = '([^']+)'/) || [])[1] || '';
const watchedSel = dtSel.split(',');
const wantSel = ['.dt-app', '.dt-topbar', '.dt-top-actions', '.dt-new',
  '.dt-omni', '.omni-body', '.omni-row', 'textarea'];
check('помощник: DT_PATCH_SEL покрывает шапку/док/поле, но не удалённые pill/send',
  wantSel.every(s => watchedSel.includes(s)) &&
  !watchedSel.includes('.dt-privacy-pill') && !watchedSel.includes('.send'), dtSel);

const needsSrc = (html.match(/function needsAssistantPatch\(n\) \{[\s\S]*?\n  \}/) || [''])[0];
check('помощник: проверка по is/contains, без closest (сообщения стрима не матчатся)',
  /n\.matches\(DT_PATCH_SEL\)/.test(needsSrc) &&
  /n\.querySelector\(DT_PATCH_SEL\)/.test(needsSrc) &&
  !/closest/.test(needsSrc));
check('помощник: планшет-сообщения не запускают патч (нет тяжёлых селекторов в воротах)',
  !/dt-msg|dt-answer|dt-paragraph|dt-table/.test(dtSel));

// ---- 2. Актуальный контракт Assistant: удалённая разметка не возвращается. ----
check('поведение: privacy pill, shield label и footer-подпись остаются удалёнными',
  !html.includes('.dt-privacy-pill') && !html.includes('.dt-private') &&
  !html.includes('.dt-local-dot') && !html.includes('LOCAL SHIELD') &&
  !html.includes('Переписка и ключи хранятся исключительно на вашем устройстве'));
check('поведение: #dtTopSub сохранён без изменения локальной и API-подписей',
  /<small id="dtTopSub">\$\{apiConnected \? 'Подключён по API · ключи на устройстве' : 'Локальный контур SHELTER · 0 байт во внешнюю сеть'\}<\/small>/.test(html));
check('поведение: «Новый чат» переносится из рейла в правую часть nav bar',
  /const nw = app\.querySelector\('\.dt-rail \.dt-new'\);/.test(html) &&
  /if \(nw\) acts\.insertBefore\(nw, acts\.firstChild\);/.test(html));
check('поведение: send остаётся в нижнем ряду, patch не переносит кнопку',
  html.includes('.dt-omni .omni-row .send{position:relative;align-self:center}') &&
  !/send\.classList\.add\('mac-send'\)/.test(html) &&
  !/body\.insertBefore\(send, row\)/.test(html));
check('поведение: авто-высота поля ввода сохранена (macGrow + ResizeObserver)',
  /ta\.dataset\.macGrow = '1'/.test(html) &&
  /new ResizeObserver\(grow\)\.observe\(ta\)/.test(html));
check('поведение: rAF-коалесcинг patchAssistant сохранён (один патч на кадр)',
  /requestAnimationFrame\(\(\) => \{ scheduled = false; patchAssistant\(\); \}\)/.test(html));
check('поведение: init/load хуки патча на месте',
  /document\.addEventListener\('DOMContentLoaded', patchAssistant\)/.test(html) &&
  /window\.addEventListener\('load', patchAssistant\)/.test(html));

// ---- 3. Пробник: одни и те же мутации на коде HEAD и рабочей копии. ----
function extractFaviconObserver(src) {
  // исторический вид: new MutationObserver(() => ensureFavicons()).observe(...)
  const direct = src.match(/new MutationObserver\(\(\) => ensureFavicons\(\)\)\.observe\(document[^;]*;/);
  if (direct) return direct[0];
  // вид с воротами (старый: classList.contains('fav'); новый: matches('.fav'))
  let anchor = src.indexOf("n.matches('.fav')");
  if (anchor < 0) anchor = src.indexOf("n.classList.contains('fav')");
  if (anchor < 0) return '';
  const start = src.lastIndexOf('new MutationObserver', anchor);
  const end = src.indexOf(';', src.indexOf('.observe(document', anchor));
  return src.slice(start, end + 1);
}
function extractAssistantObserver(src) {
  // старый вид: new MutationObserver(schedule).observe(...)
  const direct = src.match(/new MutationObserver\(schedule\)\.observe\(document\.documentElement[^;]*;/);
  const anchor = src.indexOf('needsAssistantPatch(n)) { schedule(); return; }');
  if (anchor < 0) return direct ? direct[0] : '';
  const start = src.lastIndexOf('new MutationObserver', anchor);
  const end = src.indexOf(';', src.indexOf('.observe(document.documentElement', anchor));
  return src.slice(start, end + 1);
}

function runFaviconProbe(observerSrc, mutationBatches) {
  const counters = { ensure: 0, docScan: 0, subScan: 0, raf: 0 };
  const rafQ = [];
  const sandbox = {
    MutationObserver: class { constructor(cb) { sandbox.__cb = cb; } observe() {} },
    document: { querySelectorAll: () => { counters.docScan++; return []; } },
    ensureFavicons: () => { counters.ensure++; counters.docScan++; /* имитация favScan */ },
    requestAnimationFrame: f => { counters.raf++; rafQ.push(f); },
  };
  vm.runInNewContext((/favsQueued/.test(observerSrc) ? 'let favsQueued = false;' : '') +
    observerSrc + ';', sandbox, { filename: 'favicon-observer.js' });
  for (const batch of mutationBatches) sandbox.__cb(Array.isArray(batch) ? batch : [batch]);
  while (rafQ.length) rafQ.shift()();
  return counters;
}

function runAssistantProbe(observerSrc, mutationBatches) {
  const counters = { schedule: 0 };
  const sandbox = {
    MutationObserver: class { constructor(cb) { sandbox.__cb = cb; } observe() {} },
    document: { documentElement: {} },
    schedule: () => { counters.schedule++; },
  };
  // Новый код определяет needsAssistantPatch/DT_PATCH_SEL сам; старый зовёт
  // schedule напрямую — пробник даёт оба сценария через содержимое src.
  if (/needsAssistantPatch/.test(observerSrc)) {
    const sel = (html.match(/const DT_PATCH_SEL = '([^']+)'/) || [])[1] || '';
    vm.runInNewContext(`const DT_PATCH_SEL = ${JSON.stringify(sel)};
      function needsAssistantPatch(n) {
        if (n.nodeType !== 1) return false;
        if (n.matches(DT_PATCH_SEL)) return true;
        return !!n.firstChild && !!n.querySelector(DT_PATCH_SEL);
      }
      ${observerSrc};`, sandbox, { filename: 'assistant-observer.js' });
  } else {
    vm.runInNewContext(observerSrc + ';', sandbox, { filename: 'assistant-observer.js' });
  }
  for (const batch of mutationBatches) sandbox.__cb(Array.isArray(batch) ? batch : [batch]);
  return counters;
}

// Фейковые узлы: «сообщение стрима» — не .fav и без .fav внутри; «значок» —
// сам .fav; «лист» — одиночный текстовый узел/спан без детей; «шапка
// помощника» — совпадает с DT_PATCH_SEL.
const leaf = () => ({ nodeType: 3, firstChild: null });
const node = (selfClasses, subtreeClasses, hasKids) => ({
  nodeType: 1,
  firstChild: hasKids ? leaf() : null,
  classList: { contains: c => selfClasses.includes(c) },
  matches(sel) {
    return selfClasses.some(c => new RegExp('\\.' + c + '(?![\\w-])').test(sel));
  },
  querySelector(sel) {
    return subtreeClasses.some(c => new RegExp('\\.' + c + '(?![\\w-])').test(sel)) ? leaf() : null;
  },
});

const streamMsg = () => ({ addedNodes: [node([], [], true)] });          // сообщение в .dt-app
const favBadge = () => ({ addedNodes: [node(['fav'], [], false)] });     // новый значок
const textLeaf = () => ({ addedNodes: [node([], [], false)] });          // листовая вставка
const topbar = () => ({ addedNodes: [node(['dt-topbar'], [], true)] });  // шапка помощника

const stream120 = Array.from({ length: 120 }, streamMsg);
const favs40 = Array.from({ length: 40 }, favBadge);
const leaves60 = Array.from({ length: 60 }, textLeaf);

const favNew = runFaviconProbe(extractFaviconObserver(html), stream120.concat(favs40, leaves60));
const favOld = runFaviconProbe(BASELINE_FAVICON_OBSERVER, stream120.concat(favs40, leaves60));
check('замер favicon: ensureFavicons на 120 сообщениях + 40 значках упал',
  favNew.ensure < favOld.ensure && favNew.ensure <= 2,
  `было ${favOld.ensure} -> стало ${favNew.ensure} (полных сканирований ${favOld.docScan} -> ${favNew.docScan})`);
check('замер favicon: полные сканирования DOM не растут со стримом',
  favNew.docScan <= 2, `${favOld.docScan} -> ${favNew.docScan}`);

const asstNew = runAssistantProbe(extractAssistantObserver(html), stream120.concat([topbar()]));
const asstOld = runAssistantProbe(BASELINE_ASSISTANT_OBSERVER, stream120.concat([topbar()]));
check('замер помощника: запуски патча на 120 сообщениях стрима упали',
  asstNew.schedule < asstOld.schedule && asstNew.schedule === 1,
  `было ${asstOld.schedule} запусков -> стало ${asstNew.schedule} (и 1 на шапку помощника)`);
check('замер помощника: шапка/док помощника по-прежнему запускает патч',
  asstNew.schedule === 1);

if (results.includes(false)) {
  console.error('dom_observer_budget_test: FAIL');
  process.exit(1);
}
console.log('dom_observer_budget_test: OK');
