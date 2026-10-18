// Регрессия компактной страницы «Расширения» и её фактических статусов.
// Тест читает resources/ui/index.html как текст: без браузера и зависимостей.
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

// 1. Карточка и короткие статусы — без длинных поясняющих абзацев.
const card = cut('function extCard(e, i) {', 'RENDER.extensions');
const chip = cut('function extStateChip(e, mode) {', 'function extCard(e, i) {');
check('карточка расширения существует и компактна',
  card.length > 0 && !/<p\b/.test(card) && !/desc/.test(card));
check('карточка показывает имя, версию, статус и действия',
  /e\.name/.test(card) && /e\.ver/.test(card) && /extStateChip\(e, S\.extMode\)/.test(card) &&
  /data-act="extPage"/.test(card) && /data-act="extRemove"/.test(card));
check('«Загружено» показывается только после проверки движком',
  /e\.engine && e\.passed/.test(chip) && /e\.probe === 'ok'/.test(chip) &&
  /Движок подтвердил доступность/.test(chip));
check('ошибки, ожидание перезапуска и режим оболочки имеют короткие статусы',
  /Ошибка/.test(chip) && /Проверка…/.test(chip) && /Перезапуск/.test(chip) &&
  /MV2/.test(chip) && /Скрипты/.test(chip));
check('ограниченный режим не выдаётся за поддержку полного Chrome API',
  /content-scripts/.test(chip) && !/chrome\.\* API.*работают/.test(html));

// 2. На странице нет лишнего описания и переключателя режима.
const page = cut('RENDER.extensions = ()', 'function extInstallModal() {');
check('на странице нет подзаголовка с пояснением',
  !/page-sub|Пакет распаковывается/.test(page));
check('на странице нет режима/переключателя движка',
  !/data-act="extEngine"|role="switch"|Загружать расширения движком/.test(page));
check('страница оставляет только нужные способы установки и список',
  /data-act="extStore"/.test(page) && /data-act="extInstallPop"/.test(page) &&
  /data-act="extPick"/.test(page) && /Установленные/.test(page));
check('список запрашивается у нативной оболочки до возврата разметки',
  page.indexOf('NAT.extList()') >= 0 && page.indexOf('applyExtList(r.list, r.mode)') > page.indexOf('NAT.extList()') &&
  page.indexOf('NAT.extList()') < page.indexOf('return `'));
check('страница расширений зарегистрирована в роутере',
  /extensions:/.test(cut('const PAGES = {', '};')) && /RENDER\.extensions/.test(html));

// 3. Установка и обновление списка идут через нативный bridge.
const installModal = cut('function extInstallModal() {', 'function extInstallSrc(');
check('установка по ID/ссылке открывает диалог с действующей кнопкой',
  /openModal\(\{ width: 460/.test(installModal) && /extInstallSrc\(src, b\)/.test(installModal) &&
  /extInstallPop: \(\) => extInstallModal\(\)/.test(html) &&
  /extInstallSrc: \(\) => extInstallModal\(\)/.test(html));
check('установка из файла сохраняет отдельный прямой путь',
  /extPick: \(\) => mq2\('ext\.pick'\)/.test(html) && /data-act="extPick"/.test(page));
check('список, установка и удаление используют нативные команды',
  /extList: \(\) => mq2\('ext\.list'\)/.test(html) &&
  /extInstall: src => mq2\('ext\.install'/.test(html) &&
  /extRemove: id => mq2\('ext\.remove'/.test(html));
check('событие ext обновляет список после установки/удаления',
  /case 'ext':/.test(html) && /shelterApplyExt\(d\.list, d\.mode\)/.test(html) &&
  /shelterApplyExt = \(list, mode\) => applyExtList\(list, mode\)/.test(html));

// 4. Удаление, полный список в попапе и состояние режима.
const del = cut('extRemove: async el =>', 'kbd:');
check('удаление очищает UI-список и каталог оболочки',
  /S\.ext = S\.ext\.filter/.test(del) && /NAT\.extRemove\(el\.dataset\.id\)/.test(del));
const pop = cut('function extListPop(anchor) {', 'function extPop(anchor) {');
check('иконка-пазл открывает полный список и страницу расширений',
  /S\.ext\.filter/.test(pop) && /data-page="extensions"/.test(pop));
check('список не подмешивает демо-расширения', /S\.ext = \[\];/.test(html));
check('нативный список сохраняет версию и фактический статус загрузки',
  /x\.mv \|\| 0, !!x\.engine, !!x\.passed, x\.probe \|\| 'none'/.test(html));
check('нативный режим остаётся источником статуса, но не UI-переключателем',
  /extMode: 'engine'/.test(html) &&
  /if \(mode === 'engine' \|\| mode === 'shell'\) S\.extMode = mode;/.test(html) &&
  !/data-act="extEngine"/.test(page));

// 5. Выполняем renderer статусов на данных из нативной модели.
const extMetaSrc = cut('function extMeta(e) {', 'function extCard(e, i) {');
const makeExt = () => new Function(
  'ico', 'esc', 'hostColor', 'S',
  extMetaSrc + '\n' + chip + '\n' + card + '\nreturn {chip: extStateChip, card: extCard};'
);
const icoStub = () => '';
const escStub = v => String(v == null ? '' : v);
const render = (entry, mode) => {
  const api = makeExt();
  const fn = api(icoStub, escStub, () => '#123', { extMode: mode });
  return { chip: fn.chip(entry, mode), card: fn.card(entry, 0) };
};
const mv3 = { id: 'cjpalhdlnbpafiamekefhncdjljmbd', name: 'uBlock Origin', ver: '1.60', mv: 3, engine: true, passed: true };
const states = {
  ok: render(Object.assign({}, mv3, { probe: 'ok' }), 'engine'),
  pending: render(Object.assign({}, mv3, { probe: 'pending' }), 'engine'),
  fail: render(Object.assign({}, mv3, { probe: 'fail' }), 'engine'),
  nextRun: render({ id: 'aaaa', name: 'Позже', ver: '1.0', mv: 3, engine: true, passed: false }, 'engine'),
  mv2: render({ id: 'bbbb', name: 'Старое', ver: '2.0', mv: 2 }, 'engine'),
  shell: render({ id: 'cccc', name: 'Только JS', ver: '1.0', mv: 3 }, 'shell')
};
check('проверенный MV3 получает короткий статус загрузки', /Загружено/.test(states.ok.chip));
check('ожидание движка не выдаётся за загруженное', /Проверка…/.test(states.pending.chip) && !/Загружено/.test(states.pending.chip));
check('ошибка движка отображается отдельно', /Ошибка/.test(states.fail.chip) && !/Загружено/.test(states.fail.chip));
check('новое расширение ждёт перезапуска', /Перезапуск/.test(states.nextRun.chip) && !/Загружено/.test(states.nextRun.chip));
check('MV2 и оболочечный режим явно различаются', /MV2/.test(states.mv2.chip) && /Скрипты/.test(states.shell.chip));
check('рендер карточки не добавляет длинное описание',
  Object.values(states).every(st => !/<p\b/.test(st.card)) && /v1\.60/.test(states.ok.card));

const failed = results.filter(x => !x).length;
console.log(failed ? 'UI_EXTENSIONS_TEST_FAIL ' + failed : 'UI_EXTENSIONS_TEST_PASS ' + results.length + '/' + results.length);
process.exit(failed ? 1 : 0);
