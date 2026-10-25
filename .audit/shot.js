#!/usr/bin/env node
/* Скриншотер SHELTER UI: локальный HTTP + headless Chromium (puppeteer-core).
 * Запуск: node .audit/shot.js <outDir> [probe]
 * Снимает экраны по темам; probe — только загрузка и дамп функций. */
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');

const ROOT = path.join(__dirname, '..', 'resources', 'ui');
const OUT = process.argv[2] || path.join(__dirname, 'before', 'shots');
const PROBE = process.argv.includes('probe');

const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css',
  '.woff2': 'font/woff2', '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml',
};

function serve() {
  return new Promise(res => {
    const srv = http.createServer((req, rsp) => {
      let p = decodeURIComponent(req.url.split('?')[0]);
      if (p === '/') p = '/index.html';
      const fp = path.join(ROOT, p);
      if (!fp.startsWith(ROOT) || !fs.existsSync(fp) || fs.statSync(fp).isDirectory()) {
        rsp.writeHead(404); rsp.end('404'); return;
      }
      rsp.writeHead(200, { 'Content-Type': MIME[path.extname(fp)] || 'application/octet-stream' });
      fs.createReadStream(fp).pipe(rsp);
    });
    srv.listen(0, '127.0.0.1', () => res(srv));
  });
}

(async () => {
  const puppeteer = require('/home/user/.audit-tools/node_modules/puppeteer-core');
  const chromium = require('/home/user/.audit-tools/node_modules/@sparticuz/chromium');
  const exe = await chromium.executablePath();
  const srv = await serve();
  const port = srv.address().port;
  const browser = await puppeteer.launch({
    executablePath: exe,
    args: [...chromium.args, '--no-sandbox', '--lang=ru-RU'],
    headless: true,
    defaultViewport: { width: 1440, height: 900 },
  });
  const page = await browser.newPage();
  const logs = [];
  page.on('console', m => { if (['error', 'warning'].includes(m.type())) logs.push(m.type() + ': ' + m.text().slice(0, 200)); });
  page.on('pageerror', e => logs.push('pageerror: ' + String(e).slice(0, 300)));
  await page.evaluateOnNewDocument(() => {
    try { localStorage.setItem('shelter:theme', 'graphite'); } catch (_) {}
  });
  await page.goto(`http://127.0.0.1:${port}/index.html`, { waitUntil: 'networkidle0', timeout: 30000 });
  await new Promise(r => setTimeout(r, 900));

  const probe = await page.evaluate(() => {
    const fns = ['openPage', 'openSettings', 'setTheme', 'toast', 'openModal', 'moreMenu',
      'spaceMenu', 'openPremiumDialog', 'quickPanel', 'addTaskDlg', 'wpOpenTaskCard',
      'wpSetCalendarMode', 'renderHeroLinks', 'openFind', 'heroSubmit', 'taskMenu'];
    const out = {};
    fns.forEach(f => { out[f] = typeof window[f]; });
    return {
      fns: out,
      theme: document.documentElement.getAttribute('data-theme'),
      heroVisible: !!(document.querySelector('#hero') && !document.querySelector('#hero').hidden),
      title: document.title,
      pages: Array.from(document.querySelectorAll('[data-act="page"]')).map(e => e.dataset.page).slice(0, 12),
      bodyText: document.body.innerText.slice(0, 120),
    };
  });

  if (PROBE) {
    console.log(JSON.stringify(probe, null, 2));
    console.log('--- console ---'); logs.slice(0, 20).forEach(l => console.log(l));
  } else {
    await page.evaluate(() => {
      window.__clickSel = sel => { const el = document.querySelector(sel); if (el) el.click(); return !!el; };
    });
    fs.mkdirSync(OUT, { recursive: true });
    const shot = async (name, fn) => {
      try { if (fn) await page.evaluate(fn); } catch (e) { logs.push('nav ' + name + ': ' + String(e).slice(0, 150)); }
      await new Promise(r => setTimeout(r, 450));
      await page.screenshot({ path: path.join(OUT, name + '.png') });
      console.log('shot', name);
    };
    const themes = ['graphite', 'obsidian', 'ivory', 'bordeaux'];
    for (const theme of themes) {
      await page.evaluate(t => {
        document.documentElement.setAttribute('data-theme', t);
        try { localStorage.setItem('shelter:theme', t); } catch (_) {}
      }, theme);
      await new Promise(r => setTimeout(r, 350));
      await page.evaluate(() => { document.querySelectorAll('.toast').forEach(t => t.remove()); const h = document.activeElement; if (h && h.blur) h.blur(); });
      await shot(theme + '-hero');
      await shot(theme + '-hero-focus', () => { const h = document.querySelector('#heroInput'); if (h) h.focus(); });
      await shot(theme + '-settings', () => window.__clickSel('[data-act="settings"]'));
      await shot(theme + '-settings-focus-tab', () => { const i = document.querySelector('.settings input, .settings select, .settings button'); if (i) i.focus(); });
      await page.keyboard.press('Escape');
      await shot(theme + '-tasks', () => window.__clickSel('[data-act="page"][data-page="tasks"]'));
      await shot(theme + '-calendar', () => { const s = Array.from(document.querySelectorAll('[data-seg]')).find(b => b.dataset.seg === 'month'); if (s) s.click(); });
      await shot(theme + '-task-card', () => { const t = document.querySelector('.wp-task'); if (t) t.click(); });
      await page.keyboard.press('Escape');
      await shot(theme + '-assistant', () => window.__clickSel('[data-act="page"][data-page="deepthink"]'));
      await shot(theme + '-downloads', () => window.__clickSel('[data-act="page"][data-page="downloads"]'));
      await shot(theme + '-menu', () => window.__clickSel('[data-act="more"]'));
      await page.keyboard.press('Escape');
      await shot(theme + '-toast', () => { if (window.toast) toast('Проверка тоста · аудит UI', { icon: 'check', ms: 8000 }); });
      await new Promise(r => setTimeout(r, 250));
    }
    await page.setViewport({ width: 1024, height: 720 });
    for (const theme of ['graphite', 'ivory']) {
      await page.evaluate(t => document.documentElement.setAttribute('data-theme', t), theme);
      await page.evaluate(() => { const h = document.activeElement; if (h && h.blur) h.blur(); });
      await shot('1024-' + theme + '-hero');
      await shot('1024-' + theme + '-hero-focus', () => { const h = document.querySelector('#heroInput'); if (h) h.focus(); });
      await shot('1024-' + theme + '-settings', () => { const el = document.querySelector('[data-act="settings"]'); if (el) el.click(); });
      await page.keyboard.press('Escape');
    }
    /* замер фокусного стиля #heroInput — ключевой баг: три пути фокуса */
    await page.setViewport({ width: 1440, height: 900 });
    await page.evaluate(() => document.querySelector('[data-act="page"][data-page="start"]')?.click());
    await new Promise(r => setTimeout(r, 400));
    const dumpFocus = () => page.evaluate(() => {
      const h = document.querySelector('#heroInput');
      if (!h) return { error: 'no #heroInput' };
      const cs = getComputedStyle(h);
      const omni = document.querySelector('#omni');
      return {
        active: document.activeElement && document.activeElement.id,
        outline: cs.outline, outlineStyle: cs.outlineStyle, outlineWidth: cs.outlineWidth,
        outlineColor: cs.outlineColor, boxShadow: cs.boxShadow, border: cs.border,
        omniBoxShadow: omni ? getComputedStyle(omni).boxShadow : null,
        omniClass: omni ? omni.className : null,
        focus: h.matches(':focus'), focusVisible: h.matches(':focus-visible'),
      };
    });
    const focusMatrix = {};
    await page.evaluate(() => { const h = document.querySelector('#heroInput'); if (h) h.blur(); });
    focusMatrix.programmatic = await page.evaluate(() => { const h = document.querySelector('#heroInput'); h.focus(); return true; }).then(dumpFocus);
    await page.evaluate(() => { const h = document.querySelector('#heroInput'); if (h) h.blur(); });
    const box = await page.$eval('#heroInput', el => { const r = el.getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; });
    await page.mouse.click(box.x, box.y);
    focusMatrix.mouseClick = await dumpFocus();
    await page.evaluate(() => { const h = document.querySelector('#heroInput'); if (h) h.blur(); });
    for (let i = 0; i < 12; i++) {
      await page.keyboard.press('Tab');
      const active = await page.evaluate(() => document.activeElement && document.activeElement.id);
      if (active === 'heroInput') break;
    }
    focusMatrix.keyboardTab = await dumpFocus();
    fs.writeFileSync(path.join(OUT, 'hero-focus-computed.json'), JSON.stringify(focusMatrix, null, 2));
    console.log('focusMatrix:', JSON.stringify(focusMatrix, null, 1));
    fs.writeFileSync(path.join(OUT, 'console.log.txt'), logs.join('\n'));
    console.log('console messages:', logs.length);
    logs.slice(0, 15).forEach(l => console.log(' ', l));
  }
  await browser.close();
  srv.close();
})().catch(e => { console.error('FATAL', e); process.exit(1); });
