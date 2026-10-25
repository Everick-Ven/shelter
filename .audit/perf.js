#!/usr/bin/env node
/* Фаза 4: замер UI-сценариев (longtask/метрики CDP) + проверка reduced-motion.
 * Запуск: LD_LIBRARY_PATH=/tmp/alib/lib node .audit/perf.js */
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');
const ROOT = path.join(__dirname, '..', 'resources', 'ui');
const MIME = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.woff2': 'font/woff2', '.png': 'image/png' };

(async () => {
  const puppeteer = require('/home/user/.audit-tools/node_modules/puppeteer-core');
  const chromium = require('/home/user/.audit-tools/node_modules/@sparticuz/chromium');
  const srv = http.createServer((q, r) => {
    let p = q.url.split('?')[0]; if (p === '/') p = '/index.html';
    const fp = path.join(ROOT, p);
    if (!fs.existsSync(fp) || fs.statSync(fp).isDirectory()) { r.writeHead(404); r.end(); return; }
    r.writeHead(200, { 'Content-Type': MIME[path.extname(fp)] || 'application/octet-stream' });
    fs.createReadStream(fp).pipe(r);
  });
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const browser = await puppeteer.launch({
    executablePath: await chromium.executablePath(),
    args: [...chromium.args, '--no-sandbox'], headless: true,
    defaultViewport: { width: 1440, height: 900 },
  });
  const page = await browser.newPage();
  await page.evaluateOnNewDocument(() => {
    window.__long = [];
    try {
      new PerformanceObserver(l => l.getEntries().forEach(e => window.__long.push({ n: e.name, d: Math.round(e.duration) })))
        .observe({ entryTypes: ['longtask'] });
    } catch (_) {}
  });
  const cdp = await page.target().createCDPSession();
  await cdp.send('Performance.enable');
  const metrics = async () => {
    const m = await cdp.send('Performance.getMetrics');
    const g = n => (m.metrics.find(x => x.name === n) || {}).value || 0;
    return { TaskDuration: +g('TaskDuration').toFixed(2), ScriptDuration: +g('ScriptDuration').toFixed(2), RecalcStyleCount: g('RecalcStyleCount'), RecalcStyleDuration: +g('RecalcStyleDuration').toFixed(3), LayoutCount: g('LayoutCount'), LayoutDuration: +g('LayoutDuration').toFixed(3), JSHeapUsed: Math.round(g('JSHeapUsedSize') / 1e6) };
  };
  const step = async (name, fn) => {
    const before = await metrics();
    await page.evaluate(fn);
    await new Promise(r => setTimeout(r, 650));
    const after = await metrics();
    const longs = await page.evaluate(() => { const l = window.__long.slice(); window.__long = []; return l; });
    const delta = {};
    for (const k of Object.keys(after)) delta[k] = +(after[k] - before[k]).toFixed(3);
    console.log(name, JSON.stringify(delta), 'longtasks:', JSON.stringify(longs));
  };

  await page.goto(`http://127.0.0.1:${srv.address().port}/index.html`, { waitUntil: 'networkidle0' });
  await new Promise(r => setTimeout(r, 900));
  await page.evaluate(() => window.__long = []);

  await step('open/close menu x5', async () => {});
  for (let i = 0; i < 5; i++) {
    await page.evaluate(() => document.querySelector('[data-act="more"]').click());
    await new Promise(r => setTimeout(r, 120));
    await page.keyboard.press('Escape');
    await new Promise(r => setTimeout(r, 80));
  }
  await new Promise(r => setTimeout(r, 300));
  let longs = await page.evaluate(() => { const l = window.__long.slice(); window.__long = []; return l; });
  console.log('menu x5 longtasks:', JSON.stringify(longs));

  await step('page: tasks', () => document.querySelector('[data-act="page"][data-page="tasks"]').click());
  await step('calendar month', () => { const s = Array.from(document.querySelectorAll('[data-seg]')).find(b => b.dataset.seg === 'month'); if (s) s.click(); });
  await step('page: settings', () => document.querySelector('[data-act="settings"]').click());
  await step('page: assistant', () => { document.querySelector('[data-act="page"][data-page="deepthink"]'); const el = document.querySelector('[data-act="page"][data-page="deepthink"]'); if (el) el.click(); });

  // reduced-motion: анимации должны быть выключены
  await page.emulateMediaFeatures([{ name: 'prefers-reduced-motion', value: 'reduce' }]);
  await page.evaluate(() => document.querySelector('[data-act="page"][data-page="start"]')?.click());
  await new Promise(r => setTimeout(r, 500));
  const rm = await page.evaluate(() => {
    const pick = sel => { const el = document.querySelector(sel); return el ? getComputedStyle(el).animationName : '(нет элемента)'; };
    return { orb: pick('.orb.ring'), omniRing: pick('.omni-ring'), heroCenter: pick('.hero-center'), toast: pick('.toast'),
      trans: getComputedStyle(document.querySelector('.icon-btn') || document.body).transitionProperty };
  });
  console.log('reduced-motion:', JSON.stringify(rm));
  await page.screenshot({ path: path.join(__dirname, 'tmp1024', 'reduced-motion-hero.png') });

  // perf-режим (S.prefs.gfx='perf') — контроль: что отключает
  await page.evaluate(() => { try { if (window.setGfx) setGfx('perf'); else { document.body.dataset.gfx = 'perf'; } } catch (_) { document.body.dataset.gfx = 'perf'; } });
  await new Promise(r => setTimeout(r, 400));
  const perf = await page.evaluate(() => ({ gfx: document.body.dataset.gfx, heroFx: getComputedStyle(document.querySelector('#heroFx') || document.body).display }));
  console.log('perf-mode:', JSON.stringify(perf));

  await browser.close();
  srv.close();
})().catch(e => { console.error('FATAL', e); process.exit(1); });
