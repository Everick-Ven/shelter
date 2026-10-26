'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources', 'ui', 'index.html'), 'utf8');
const renderStart = html.indexOf('RENDER.deepthink = m => {');
const renderEnd = html.indexOf('\n};', renderStart);
assert(renderStart >= 0 && renderEnd > renderStart, 'the active Assistant page renderer must be present');
const render = html.slice(renderStart, renderEnd);
assert(!render.includes('dt-privacy-pill') && !render.includes('Приватный контур'),
  'the static privacy pill is absent from the Assistant header markup');
assert(render.includes('id="dtTopSub"'), 'the existing Assistant subtitle remains present');
assert(render.includes("'Локальный контур SHELTER · 0 байт во внешнюю сеть'"),
  'the pre-existing local-mode subtitle is intentionally unchanged pending approval');
assert(render.includes("'Подключён по API · ключи на устройстве'"),
  'the pre-existing API-mode subtitle is intentionally unchanged pending approval');
assert(render.includes('class="dt-private" data-tip="Локальная защита контекста"'),
  'the separate contextual privacy indicator in the Assistant rail is preserved');

const patchStart = html.indexOf('function patchAssistant() {');
const patchEnd = html.indexOf('\n  }', patchStart);
assert(patchStart >= 0 && patchEnd > patchStart, 'the Assistant post-render patcher must be present');
const patch = html.slice(patchStart, patchEnd);
assert(!patch.includes('dt-privacy-pill') && !patch.includes('Приватный контур'),
  'patchAssistant no longer relocates or recreates the removed pill');
assert(patch.includes("app.querySelector('.dt-rail .dt-new')"),
  'the New Conversation button still moves into the right-side actions');

const styles = [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)].map(match => match[1]).join('\n');
assert(!styles.includes('.dt-privacy-pill'), 'all static-pill CSS has been removed');
assert.match(styles, /\.dt-topbar\{[^}]*grid-template-columns:minmax\(0,1fr\) auto minmax\(0,1fr\)/,
  'the Assistant title keeps its balanced centered-header grid');
assert.match(styles, /\.dt-topbar>\.dt-top-title\{grid-column:2;grid-row:1/,
  'removing the pill does not change the title column');

console.log('Assistant header privacy-pill and subtitle regression tests passed');
