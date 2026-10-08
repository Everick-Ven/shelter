// Регрессия страницы «Расширения» и её честных подписей.
//
// Контекст: CEF вырезал API расширений (~M127), поэтому MV3-пакеты оболочка
// передаёт движку аргументом --load-extension, а MV2 (их Chromium 154 не грузит)
// остаются на ручном внедрении content-scripts. Страница не должна обещать
// большего, чем происходит. Здесь стерегём:
//   1) статус карточки — факт, а не обещание: «Загружено движком» только когда
//      путь реально попал в --load-extension (engine && passed), иначе «со
//      следующего запуска» / «Оболочка · MV2» / «Оболочка · content-scripts»;
//   2) у страницы есть переключатель режима, и он ходит в нативный мост
//      ext.setMode, а не пишет UI-настройку в localStorage;
//   3) список и режим приходят из нативного сканирования (ext.list) и
//      обновляются событием ext;
//   4) удаление расширения чистит и UI-состояние, и каталог оболочки;
//   5) строка магазина и подзаголовок страницы говорят, кто именно исполняет
//      пакет, и предупреждают про неподдерживаемые chrome.* API/фон.
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

// 1. Карточка установленного расширения.
const card = cut('function extCard(e, i) {', 'RENDER.extensions');
check('карточка расширения существует', card.length > 0, card.length + ' символов');
check('карточка говорит, что исполняются content-scripts',
  /content-scripts/.test(card));
check('карточка не называет chrome.* API поддержанными',
  /chrome\.\* API/.test(card) || /chrome\.\* API/.test(html));
// Статус рисует extStateChip: «Загружено движком» возможно только в ветке
// engine && passed, то есть когда путь реально передан в --load-extension.
const chip = cut('function extStateChip(e, mode) {', 'function extCard(e, i) {');
check('статус-чип существует и учитывает manifest v2',
  chip.length > 0 && /e\.engine && e\.passed/.test(chip) && /MV2/.test(chip));
check('«Загружено движком» только когда движок подтвердил загрузку',
  /e\.probe === 'ok'/.test(chip) && /Загружено движком/.test(chip) &&
  /Движок не подтвердил/.test(chip) && /Проверяю движок/.test(chip) &&
  /Со следующего запуска/.test(chip) && !/Включено/.test(chip));
check('«Загружено» не выставляется по одному лишь аргументу --load-extension',
  !/if \(e\.engine && e\.passed\) return `<span class="ext-state">/.test(chip));
check('карточка берёт статус из чипа, а не пишет «Включено»',
  /extStateChip\(e, S\.extMode\)/.test(card) && !/Включено/.test(card));
check('карточка показывает версию манифеста, когда она известна',
  /manifest v/.test(card) && /e\.mv/.test(card));
check('в карточке нет формулировки «Chromium extension» (ложное обещание)',
  !/Chromium extension/.test(html));

// 2. Подзаголовок страницы «Расширения».
const page = cut('RENDER.extensions = ()', 'function extInstallSrc(');
check('страница расширений описывает распаковку локально',
  /распаковывается локально/.test(page));
check('страница честно говорит, что MV3 грузит движок Chromium',
  /MV3 грузит движок Chromium/.test(page) && /--load-extension/.test(page));
check('переключатель режима есть и ходит в нативный мост',
  /data-act="extEngine"/.test(page) && /NAT\.extSetMode/.test(html) &&
  /extSetMode: function \(engine\) \{ return q\('ext\.setMode'/.test(fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'host-bridge.js'), 'utf8')));
check('подпись режима предупреждает про перезапуск',
  /после перезапуска/.test(page) && /фиксируется при запуске/.test(page));
check('подпись режима объясняет проверку загрузки движком',
  /подтверждает запросом к самому расширению/.test(page) &&
  /chrome-extension:\/\/…\/manifest\.json/.test(page));
check('страница не обещает «настоящие расширения Chromium»',
  !/Настоящие расширения Chromium/.test(html));
const installModal = cut('function extInstallModal() {', 'function extInstallSrc(');
check('страница предлагает установку из магазина и из файла',
  /data-act="extPick"/.test(page) && /data-act="extInstallPop"/.test(page) &&
  /data-act="extStore"/.test(page));
check('установка по ID открывает попап, а не строку на странице',
  installModal.length > 0 && /openModal\(\{ width: 460/.test(installModal) &&
  /extInstallSrc\(src, b\)/.test(installModal) && /closeModal\(m\)/.test(installModal) &&
  /extInstallPop: \(\) => extInstallModal\(\)/.test(html) &&
  /extInstallSrc: \(\) => extInstallModal\(\)/.test(html));
check('список и режим приходят из нативного сканирования',
  /NAT\.extList\(\)/.test(page) && /applyExtList\(r\.list, r\.mode\)/.test(page));
check('строка магазина предупреждает про chrome.* API',
  /chrome\.\* API не поддерживаются/.test(page));
check('страница говорит, кто исполнит пакет каждого типа',
  /MV3 грузит движок Chromium, MV2 применяет оболочка/.test(page) &&
  /MV2 движок не грузит политикой Chromium/.test(page) &&
  /новое расширение подхватывается после перезапуска/.test(page));

// 3. Мост: NAT.ext* — те же команды, что обрабатывает shell_bridge.cc.
const nat = cut('extList: () => mq2(', 'extPick: () => mq2(');
check('ext.list/ext.install/ext.remove/ext.pick уходят в mq2',
  /extList: \(\) => mq2\('ext\.list'\)/.test(html) &&
  /extInstall: src => mq2\('ext\.install'/.test(html) &&
  /extRemove: id => mq2\('ext\.remove'/.test(html) &&
  /extPick: \(\) => mq2\('ext\.pick'\)/.test(html), nat.length + ' символов');
check('событие ext из оболочки обновляет список и режим',
  /case 'ext':/.test(html) &&
  /shelterApplyExt\(d\.list, d\.mode\)/.test(html) &&
  /shelterApplyExt = \(list, mode\) => applyExtList\(list, mode\)/.test(html));

// 4. Удаление и попап.
const del = cut('extRemove: async el =>', 'kbd:');
check('удаление чистит UI-список и каталог оболочки',
  /S\.ext = S\.ext\.filter/.test(del) && /NAT\.extRemove\(el\.dataset\.id\)/.test(del));
const pop = cut('function extListPop(anchor) {', 'function extPop(anchor) {');
check('попап иконки-пазла показывает все установленные расширения',
  /openMenu\(anchor/.test(pop) && /S\.ext\.filter/.test(pop) &&
  /data-page="extensions"/.test(pop));
check('подписи попапов не обещают content-scripts вместо движка',
  !/Chrome-расширение · content-scripts/.test(html) &&
  /manifest v' \+ e\.mv/.test(pop) &&
  /extStateChip\(e, S\.extMode\)/.test(cut('function extPop(anchor) {', 'function applyExtList(')));
check('демо-расширения не подмешиваются',
  /S\.ext = \[\];/.test(html));
check('сигнатура списка учитывает версию манифеста, статус движка и проверку',
  /x\.mv \|\| 0, !!x\.engine, !!x\.passed, x\.probe \|\| 'none'/.test(html));
check('режим по умолчанию — движок, но нативное значение важнее',
  /extMode: 'engine'/.test(html) && /if \(mode === 'engine' \|\| mode === 'shell'\) S\.extMode = mode;/.test(html));

// 5. Вёрстка страницы доступна из «Все расширения» и из палитры.
const pages = cut('const PAGES = {', '};');
check('страница extensions зарегистрирована в роутере страниц',
  /extensions:/.test(pages) && /RENDER\.extensions/.test(html));

// 6. Статус расширения — факт, а не обещание: прогоняем extStateChip()/
//    extCard() с подставными зависимостями и проверяем все состояния из
//    нативной модели (engine/passed/probe) и обоих режимов (engine/shell).
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
  return { chip: fn.chip(entry, mode), card: fn.card(entry, 0), desc: fn.card(entry, 0).match(/<p>([\s\S]*?)<\/p>/) };
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
check('подтверждённый движком MV3 — «Загружено движком»',
  /Загружено движком/.test(states.ok.chip), states.ok.chip);
check('непроверенный MV3 не выдаётся за загруженный',
  /Проверяю движок/.test(states.pending.chip) && !/Загружено/.test(states.pending.chip), states.pending.chip);
check('проваленная проверка движка называется честно',
  /Движок не подтвердил/.test(states.fail.chip) && !/Загружено/.test(states.fail.chip), states.fail.chip);
check('путь, ждущий перезапуска, не называется загруженным',
  /Со следующего запуска/.test(states.nextRun.chip) && !/Загружено/.test(states.nextRun.chip), states.nextRun.chip);
check('MV2 в режиме движка подписан политикой Chromium',
  /MV2/.test(states.mv2.chip) && !/Загружено/.test(states.mv2.chip), states.mv2.chip);
check('режим оболочки подписан content-scripts',
  /content-scripts/.test(states.shell.chip) && !/Загружено/.test(states.shell.chip), states.shell.chip);
check('карточка объясняет, кто исполнит пакет',
  /Manifest v3/.test(states.ok.desc[1]) && /content-scripts/i.test(states.shell.desc[1]) &&
  /service worker/.test(states.shell.desc[1]) && /content-scripts/.test(states.ok.desc[1]),
  states.shell.desc[1].slice(0, 80));
check('карточка показывает версию манифеста, когда она известна',
  /manifest v3/.test(states.ok.card) && /manifest v2/.test(states.mv2.card));
check('ни одна карточка не пишет просто «Загружено»',
  Object.values(states).every(st => !/>\s*Загружено\s*</.test(st.card)));

const failed = results.filter(x => !x).length;
console.log(failed ? 'UI_EXTENSIONS_TEST_FAIL ' + failed : 'UI_EXTENSIONS_TEST_PASS ' + results.length + '/' + results.length);
process.exit(failed ? 1 : 0);
