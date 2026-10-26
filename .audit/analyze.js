#!/usr/bin/env node
/* Аудит-анализатор SHELTER UI: контраст токенов и счётчики эффектов.
 * Запуск: node .audit/analyze.js [path-to-index.html]
 * Печатает таблицу контраста WCAG и метрики; без зависимостей. */
'use strict';
const fs = require('fs');
const path = require('path');

const file = process.argv[2] || path.join(__dirname, '..', 'resources', 'ui', 'index.html');
const html = fs.readFileSync(file, 'utf8');

/* ---- цвета ---- */
function parseColor(v) {
  v = String(v || '').trim();
  let m = v.match(/^#([0-9a-fA-F]{6})$/);
  if (m) return [parseInt(m[1].slice(0, 2), 16), parseInt(m[1].slice(2, 4), 16), parseInt(m[1].slice(4, 6), 16)];
  m = v.match(/^#([0-9a-fA-F]{3})$/);
  if (m) return [parseInt(m[1][0] + m[1][0], 16), parseInt(m[1][1] + m[1][1], 16), parseInt(m[1][2] + m[1][2], 16)];
  m = v.match(/^rgba?\(([^)]+)\)$/i);
  if (m) {
    const p = m[1].split(',').map(s => parseFloat(s));
    return [p[0] | 0, p[1] | 0, p[2] | 0];
  }
  return null;
}
function lum([r, g, b]) {
  const f = c => {
    c /= 255;
    return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b);
}
function ratio(fg, bg) {
  const a = lum(fg), b = lum(bg);
  const hi = Math.max(a, b), lo = Math.min(a, b);
  return (hi + 0.05) / (lo + 0.05);
}

const themes = {};
const themeRe = /html\[data-theme="([a-z]+)"\]\{([\s\S]*?)\n\}/g;
let tm;
while ((tm = themeRe.exec(html))) {
  const vars = {};
  const varRe = /--([a-z0-9-]+)\s*:\s*([^;]+);/g;
  let vm;
  while ((vm = varRe.exec(tm[2]))) vars['--' + vm[1]] = vm[2].trim();
  if (vars['--bg']) themes[tm[1]] = vars;
}

const wanted = ['--bg', '--surface', '--card', '--card-2', '--text', '--text-2', '--text-3', '--text-4'];
console.log('== Контраст токенов (WCAG) ==');
const header = ['theme', ...wanted.map(w => w.replace('--', ''))].join('\t');
console.log(header);
const contrastRows = {};
for (const [name, vars] of Object.entries(themes)) {
  const colors = {};
  for (const w of wanted) colors[w] = parseColor(vars[w]);
  const row = {};
  for (const fgName of ['--text', '--text-2', '--text-3', '--text-4']) {
    row[fgName] = {};
    for (const bgName of ['--bg', '--card', '--card-2']) {
      const fg = colors[fgName], bg = colors[bgName];
      row[fgName][bgName] = fg && bg ? ratio(fg, bg) : null;
    }
  }
  contrastRows[name] = row;
  const cells = wanted.map(w => {
    if (w === '--bg' || w === '--surface' || w === '--card' || w === '--card-2') return vars[w] || '';
    const r = row[w] && row[w]['--bg'];
    return r ? r.toFixed(2) : '';
  });
  console.log([name, ...cells].join('\t'));
}
console.log('\n== Контраст по поверхностям (bg / card / card-2) ==');
console.log('theme\ttext-2\ttext-3\ttext-4');
for (const [name, row] of Object.entries(contrastRows)) {
  const f = k => (row[k]['--bg'] || 0).toFixed(2) + '/' + (row[k]['--card'] || 0).toFixed(2) + '/' + (row[k]['--card-2'] || 0).toFixed(2);
  console.log(name + '\t' + f('--text-2') + '\t' + f('--text-3') + '\t' + f('--text-4'));
}

/* ---- счётчики эффектов ---- */
const count = re => (html.match(re) || []).length;
console.log('\n== Метрики эффектов ==');
const metrics = {
  'backdrop-filter': count(/backdrop-filter\s*:/g),
  'filter:blur / blur(': count(/blur\(/g),
  'box-shadow': count(/box-shadow\s*:/g),
  'animation: (объявления)': count(/(?:^|[;{])\s*animation\s*:/gm),
  '@keyframes': count(/@keyframes/g),
  '!important': count(/!important/g),
  'font-size 9px': count(/font-size\s*:\s*9px/g),
  'font-size 10px': count(/font-size\s*:\s*10px/g),
  'font-size 11px': count(/font-size\s*:\s*11px/g),
  'font-size 11.5px': count(/font-size\s*:\s*11\.5px/g),
  'font-size 12px': count(/font-size\s*:\s*12px/g),
  'transition: all': count(/transition\s*:\s*all/g),
  'ease-in': count(/ease-in(?!\w|-)/g),
  'will-change': count(/will-change\s*:/g),
  'prefers-reduced-motion': count(/prefers-reduced-motion/g),
  'infinite animations': count(/animation[^;]*\binfinite\b/g),
};
for (const [k, v] of Object.entries(metrics)) console.log(k + '\t' + v);

/* ---- использование --text-4 ---- */
console.log('\n== var(--text-4) в CSS ==');
const css = (html.match(/<style[^>]*>([\s\S]*?)<\/style>/g) || []).join('\n');
const t4 = [];
const cssBlockRe = /var\(--text-4\)/g;
let idx = 0, found;
while ((found = cssBlockRe.exec(css))) {
  const start = Math.max(0, css.lastIndexOf('\n', found.index));
  t4.push(css.slice(start + 1, css.indexOf('\n', found.index)).trim().slice(0, 110));
  if (++idx > 60) break;
}
t4.forEach(l => console.log('  ' + l));
