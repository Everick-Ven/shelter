'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');
const results = [];
const check = (name, condition) => {
  results.push(!!condition);
  console.log((condition ? 'PASS ' : 'FAIL ') + name);
};
const cut = (from, to) => {
  const i = html.indexOf(from);
  if (i < 0) return '';
  const j = to ? html.indexOf(to, i) : -1;
  return html.slice(i, j < 0 ? html.length : j);
};
const helper = cut('function setupToastMarquee(clip, text) {', 'function toast(msg, opts = {}) {');
const toastFn = cut('function toast(msg, opts = {}) {', '/* подсказки */');
const css = html.slice(html.indexOf('.toast .toast-text{'), html.indexOf('@keyframes toastIn'));

check('toast has a clipped single-line text viewport',
  /\.toast \.toast-text\{[^}]*overflow:hidden[^}]*white-space:nowrap/.test(css));
check('only overflowing text receives the marquee animation',
  /scrollWidth - clip\.clientWidth/.test(helper) && /overflow < 2/.test(helper) &&
  /classList\.add\('marquee'\)/.test(helper) && /toastMarquee/.test(css));
check('toast markup keeps message text escaped and measurable',
  /class="toast-text"><span class="toast-marquee-text">\$\{esc\(msg\)\}/.test(toastFn));
check('marquee measurement runs after layout and gives long messages time to scroll',
  /raf\(\(\) =>/.test(toastFn) && /setupToastMarquee\(/.test(toastFn) &&
  /Math\.max\(lifetime, cycle \+ 800\)/.test(toastFn));
check('performance and system reduced-motion settings suppress movement',
  /REDUCED\(\)/.test(helper) && /prefers-reduced-motion: reduce/.test(helper) &&
  /@media\(prefers-reduced-motion:reduce\)/.test(css));

const make = (reduced, systemReduced = false) => new Function(
  'REDUCED', 'window', helper + '\nreturn setupToastMarquee;'
)(() => reduced, { matchMedia: () => ({ matches: systemReduced }) });
const makeNodes = (clipWidth, textWidth) => {
  const values = {};
  const classes = new Set();
  return {
    clip: {
      clientWidth: clipWidth,
      style: { setProperty: (key, value) => { values[key] = value; } },
      classList: { add: name => classes.add(name) }
    },
    text: { scrollWidth: textWidth },
    values,
    classes
  };
};
const fits = makeNodes(120, 118);
assert.equal(make(false)(fits.clip, fits.text), 0);
assert.equal(fits.classes.size, 0);
assert.equal(Object.keys(fits.values).length, 0);
check('a fitting notification is not animated', fits.classes.size === 0);

const overflows = makeNodes(120, 320);
const cycle = make(false)(overflows.clip, overflows.text);
assert.ok(cycle > 0);
assert.equal(overflows.values['--toast-shift'], '-200px');
assert.ok(overflows.classes.has('marquee'));
check('an overflowing notification gets a measured scroll distance',
  overflows.values['--toast-shift'] === '-200px' && overflows.classes.has('marquee'));

const reduced = makeNodes(120, 320);
assert.equal(make(true)(reduced.clip, reduced.text), 0);
assert.equal(reduced.classes.size, 0);
const systemReduced = makeNodes(120, 320);
assert.equal(make(false, true)(systemReduced.clip, systemReduced.text), 0);
check('reduced-motion preferences leave text still',
  reduced.classes.size === 0 && systemReduced.classes.size === 0);

const failed = results.filter(x => !x).length;
console.log(failed ? `UI_TOAST_MARQUEE_TEST_FAIL ${failed}` : `UI_TOAST_MARQUEE_TEST_PASS ${results.length}/${results.length}`);
process.exit(failed ? 1 : 0);
