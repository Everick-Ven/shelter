'use strict';

// Обводка выбранного цвета в диалоге «Приоритет» — как у статусов.
//
// Что стережём:
//   1) клик по цвету переносит обводку .wp-swatch.is-on на выбранный тон
//      (change-обработчик, зеркальный wpStatusDlg);
//   2) при открытии обведён текущий цвет: точное совпадение с тоном палитры —
//      сам тон; цвет вне палитры (тема/свой) — свотч «Текущий цвет»;
//   3) сохранение без кликов не меняет цвет (свотч текущего цвета отмечен);
//   4) CSS-кольцо то же, что у статус: .wp-swatch.is-on (пинится в ui_css_test).
//
// Тест исполняет wpPriorityDlg в VM с мини-DOM: без браузера, без зависимостей.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const results = [];
const check = (name, ok, detail) => {
  results.push(!!ok);
  console.log((ok ? 'PASS ' : 'FAIL ') + name + (detail !== undefined ? ' :: ' + detail : ''));
};

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');

// ---- 1. Статистические пины. ----
const dlgStart = html.indexOf('function wpPriorityDlg({');
const dlgEnd = html.indexOf('\n/* Удаление занятого приоритета', dlgStart);
const dlgSrc = html.slice(dlgStart, dlgEnd);
check('wpPriorityDlg найден', dlgStart >= 0 && dlgEnd > dlgStart, dlgSrc.length + ' символов');
check('обводка следует за кликом — change-обработчик как у статусов',
  /root\.addEventListener\('change', e => \{ if \(!e\.target\.closest\('\[data-wp-prio-pick\]'\)\) return; root\.querySelectorAll\('\[data-wp-prio-pick\]'\)\.forEach\(el => el\.closest\('\.wp-swatch'\)\.classList\.toggle\('is-on', el\.checked\)\); \}\)/.test(dlgSrc));
const statusDlgStart = html.indexOf('function wpStatusDlg({');
const statusDlgEnd = html.indexOf('\n/* Строка справочника статусов', statusDlgStart);
check('обработчик зеркален wpStatusDlg (та же механика toggle)',
  html.slice(statusDlgStart, statusDlgEnd).includes(`el.closest('.wp-swatch').classList.toggle('is-on', el.checked)`));
check('текущий цвет вне палитры получает свой свотч с обводкой',
  /const inPalette = WP_PRIORITY_SWATCHES\.some\(/.test(dlgSrc) &&
  /inPalette \? '' : `<label class="wp-swatch is-on" style="--sw:\$\{colorInit\}" title="Текущий цвет">/.test(dlgSrc));
check('точное совпадение с тоном по-прежнему обводит сам тон',
  /tone\.toLowerCase\(\) === String\(colorInit\)\.toLowerCase\(\) \? ' is-on' : ''/.test(dlgSrc));
check('логика сохранения не менялась (checked -> тон, иначе colorInit)',
  /const pick = root\.querySelector\('\[data-wp-prio-pick\]:checked'\);/.test(dlgSrc) &&
  /color: pick \? wpValidPriorityColor\(pick\.value\) : colorInit/.test(dlgSrc));

// ---- 2. Пробник: открытие -> клик -> сохранение. ----
function runDialog({ color }) {
  // Мини-DOM: разбираем свотчи из собранной разметки и храним состояние радио.
  const scrim = {
    listeners: {},
    addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); },
    nameEl: { value: 'Срочный', addEventListener() {}, focus() {}, select() {} },
    swatches: [], // { label: {classList}, input: {checked, value, closest} }
    querySelector(sel) {
      if (sel === '[data-wp-name]') return this.nameEl;
      if (sel === '[data-wp-prio-pick]:checked') {
        const on = this.swatches.find(s => s.input.checked);
        return on ? on.input : null;
      }
      return null;
    },
    querySelectorAll(sel) {
      if (sel === '[data-wp-prio-pick]') return this.swatches.map(s => s.input);
      return [];
    },
  };
  const sandbox = {
    WP_PRIORITY_SWATCHES: ['#9ca3af', '#3b82f6', '#8b5cf6', '#eab308', '#f97316', '#ef4444'],
    wpValidPriorityColor(value) { const s = String(value || '').trim(); return /^#[0-9a-fA-F]{6}$/.test(s) ? s : ''; },
    esc: s => String(s == null ? '' : s),
    toast() {},
    closeModal() {},
    setTimeout() {},
    openModal(options) {
      const h = options.html;
      const swRe = /<label class="wp-swatch([^"]*)" style="--sw:([^"]*)" title="([^"]*)"><input class="sr-only" type="radio" name="wpPrioColor" data-wp-prio-pick value="([^"]*)"( checked)?>/g;
      let m;
      while ((m = swRe.exec(h))) {
        const classes = new Set(m[1].split(' ').filter(Boolean));
        const label = {
          classList: {
            toggle(c, force) { if (force === undefined) { classes.has(c) ? classes.delete(c) : classes.add(c); } else if (force) classes.add(c); else classes.delete(c); },
            contains(c) { return classes.has(c); },
          },
        };
        const input = { checked: !!m[5], value: m[4], closest: () => label };
        scrim.swatches.push({ label, input, tone: m[2], title: m[3] });
      }
      return { scrim, guard: null };
    },
  };
  vm.runInNewContext(dlgSrc + '\n;this.__open = wpPriorityDlg;', sandbox, { filename: 'wpPriorityDlg.js' });
  return { scrim, open: sandbox.__open };
}

function clickTone(scrim, value) {
  scrim.swatches.forEach(s => { s.input.checked = s.input.value === value; });
  (scrim.listeners.change || []).forEach(fn => fn({ target: scrim.swatches.find(s => s.input.value === value).input }));
}

function saveDlg(scrim, openPromise) {
  (scrim.listeners.click || []).forEach(fn => fn({ target: { closest: () => ({ dataset: { r: '1' } }) } }));
  return openPromise;
}

// 2a. Цвет вне палитры (тема): свотч «Текущий цвет» обведён и отмечен.
(async () => {
  const w1 = runDialog({ color: '#f5a524' });
  const p1 = w1.open({ title: 'Приоритет', name: 'Срочный', color: '#f5a524' });
  check('вне палитры: 7 свотчей, первый — «Текущий цвет» с is-on',
    w1.scrim.swatches.length === 7 &&
    w1.scrim.swatches[0].title === 'Текущий цвет' &&
    w1.scrim.swatches[0].label.classList.contains('is-on') &&
    w1.scrim.swatches[0].tone === '#f5a524',
    w1.scrim.swatches.map(s => s.tone).join(' '));
  check('вне палитры: текущий цвет отмечен радио (сохранение ничего не сломает)',
    w1.scrim.swatches[0].input.checked);

  // Клик по тону — обводка переходит.
  clickTone(w1.scrim, '#eab308');
  const ring = w1.scrim.swatches.map(s => s.label.classList.contains('is-on'));
  check('клик по цвету переносит обводку (как у статусов)',
    ring.filter(Boolean).length === 1 && ring[4] === true && ring[0] === false,
    'ring=' + ring.map((v, i) => (v ? w1.scrim.swatches[i].tone : '')).filter(Boolean).join(','));
  const r1 = await saveDlg(w1.scrim, p1);
  check('сохранение после клика пишет выбранный тон',
    r1 && r1.name === 'Срочный' && r1.color === '#eab308', JSON.stringify(r1));

  // 2b. Сохранение без кликов — цвет не меняется.
  const w2 = runDialog({ color: '#f5a524' });
  const p2 = w2.open({ title: 'Приоритет', name: 'Срочный', color: '#f5a524' });
  const r2 = await saveDlg(w2.scrim, p2);
  check('сохранение без кликов хранит прежний цвет',
    r2 && r2.color === '#f5a524', JSON.stringify(r2));

  // 2c. Тон палитры: свотч «Текущий цвет» не нужен, обведён сам тон.
  const w3 = runDialog({ color: '#ef4444' });
  const p3 = w3.open({ title: 'Приоритет', name: 'Срочный', color: '#ef4444' });
  check('тон палитры: 6 свотчей, обведён и отмечен сам тон',
    w3.scrim.swatches.length === 6 &&
    w3.scrim.swatches[5].tone === '#ef4444' &&
    w3.scrim.swatches[5].label.classList.contains('is-on') &&
    w3.scrim.swatches[5].input.checked);
  const r3 = await saveDlg(w3.scrim, p3);
  check('сохранение без кликов хранит тон палитры',
    r3 && r3.color === '#ef4444', JSON.stringify(r3));

  if (results.includes(false)) {
    console.error('priority_color_pick_test: FAIL');
    process.exit(1);
  }
  console.log('priority_color_pick_test: OK');
})();
