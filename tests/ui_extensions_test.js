// Регрессия страницы «Расширения» и её честных подписей.
//
// Контекст: CEF вырезал API расширений (~M127), поэтому оболочка сама применяет
// content-scripts из пакета, а движок расширения не загружает. Страница не должна
// обещать обратного, иначе получается «UI-слой», который висит отдельно от
// браузера. Здесь стерегём:
//   1) карточка и попап говорят, что именно исполняется (content-scripts), и не
//      называют пакет загруженным в движок;
//   2) подзаголовок страницы не обещает «настоящие расширения Chromium»;
//   3) мост NAT.ext* остаётся подключён к host-bridge (mq2), а список приходит
//      из нативного сканирования каталога и перерисовывает страницу;
//   4) удаление расширения чистит и UI-состояние, и каталог оболочки;
//   5) строка магазина предупреждает, что chrome.* API и фоновые страницы не
//      поддерживаются (иначе пользователь ждёт от пакета большего, чем возможно).
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
check('статус расширения не утверждает загрузку в движок',
  /ext-state">\$\{ico\('check'\)\}Включено/.test(card) && !/Загружено/.test(card));
check('в карточке нет формулировки «Chromium extension» (ложное обещание)',
  !/Chromium extension/.test(html));

// 2. Подзаголовок страницы «Расширения».
const page = cut('RENDER.extensions = ()', 'function extInstallSrc(');
check('страница расширений описывает распаковку локально',
  /распаковывается локально/.test(page));
check('страница не обещает «настоящие расширения Chromium»',
  !/Настоящие расширения Chromium/.test(html));
check('страница предлагает установку из магазина и из файла',
  /data-act="extPick"/.test(page) && /data-act="extInstallSrc"/.test(page) &&
  /data-act="extStore"/.test(page));
check('список установленных приходит из нативного сканирования',
  /NAT\.extList\(\)/.test(page) && /applyExtList\(r\.list\)/.test(page));
check('строка магазина предупреждает про chrome.* API',
  /chrome\.\* API не поддерживаются/.test(page));

// 3. Мост: NAT.ext* — те же команды, что обрабатывает shell_bridge.cc.
const nat = cut('extList: () => mq2(', 'extPick: () => mq2(');
check('ext.list/ext.install/ext.remove/ext.pick уходят в mq2',
  /extList: \(\) => mq2\('ext\.list'\)/.test(html) &&
  /extInstall: src => mq2\('ext\.install'/.test(html) &&
  /extRemove: id => mq2\('ext\.remove'/.test(html) &&
  /extPick: \(\) => mq2\('ext\.pick'\)/.test(html), nat.length + ' символов');
check('событие ext из оболочки обновляет список',
  /case 'ext':/.test(html) && /shelterApplyExt/.test(html));

// 4. Удаление и попап.
const del = cut('extRemove: async el =>', 'kbd:');
check('удаление чистит UI-список и каталог оболочки',
  /S\.ext = S\.ext\.filter/.test(del) && /NAT\.extRemove\(el\.dataset\.id\)/.test(del));
const pop = cut('function extListPop(anchor) {', 'function extPop(anchor) {');
check('попап иконки-пазла показывает все установленные расширения',
  /openMenu\(anchor/.test(pop) && /S\.ext\.filter/.test(pop) &&
  /data-page="extensions"/.test(pop));
check('демо-расширения не подмешиваются',
  /S\.ext = \[\];/.test(html));

// 5. Вёрстка страницы доступна из «Все расширения» и из палитры.
const pages = cut('const PAGES = {', '};');
check('страница extensions зарегистрирована в роутере страниц',
  /extensions:/.test(pages) && /RENDER\.extensions/.test(html));

const failed = results.filter(x => !x).length;
console.log(failed ? 'UI_EXTENSIONS_TEST_FAIL ' + failed : 'UI_EXTENSIONS_TEST_PASS ' + results.length + '/' + results.length);
process.exit(failed ? 1 : 0);
