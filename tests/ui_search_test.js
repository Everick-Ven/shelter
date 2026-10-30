'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const html = fs.readFileSync(path.join(__dirname, '..', 'resources/ui/index.html'), 'utf8');
const engineSource = (html.match(/const ENGINES = \{[\s\S]*?\n\};/) || [])[0];
const resolveInputSource = (html.match(/function resolveInput\(raw\) \{[\s\S]*?\n\}/) || [])[0];
const searchOfSource = (html.match(/function searchOf\(url\) \{[\s\S]*?\n\}/) || [])[0];
assert(engineSource && resolveInputSource && searchOfSource, 'search routing helpers must be present');
const engines = new Function(`${engineSource}; return ENGINES;`)();
const resolveInput = new Function('S', 'ENGINES', 'isUrl', 'internalPage',
  `${resolveInputSource}; return resolveInput;`)({ deep: false, engine: 'yandex' }, engines,
  () => false, () => ({ page: 'start', q: '' }));
const searchOf = new Function('ENGINES', `${searchOfSource}; return searchOf;`)(engines);
const query = 'шторм + тест & "кавычки" / %';
const yandexUrl = resolveInput(query);
assert.equal(yandexUrl, 'https://yandex.ru/search/?text=' + encodeURIComponent(query),
  'Yandex search uses the standard endpoint with a single URI-encoded query');
const yandexParsed = new URL(yandexUrl);
assert.equal(yandexParsed.pathname, '/search/');
assert.deepEqual(Array.from(yandexParsed.searchParams.keys()), ['text']);
assert.deepEqual(searchOf(yandexUrl), { engine: 'yandex', q: query },
  'the encoded request round-trips through search-result detection without changing its query');

const button = html.match(/<button type="button" class="sb-search"[\s\S]*?<\/button>/);
assert(button, 'the browser-search button must exist');
assert(button[0].includes('aria-label="Поиск по браузеру"'));
assert(button[0].includes('data-tip="Поиск по браузеру · ⌘K"'));
assert(button[0].includes('<span class="hc">Поиск по браузеру</span>'));

const paletteStart = html.indexOf('function openPalette() {');
const paletteEnd = html.indexOf('\nconst FIND =', paletteStart);
assert(paletteStart >= 0 && paletteEnd > paletteStart, 'the browser search palette must be present');
const palette = html.slice(paletteStart, paletteEnd);
assert(palette.includes('aria-label="Поиск по браузеру"'));
assert(palette.includes('placeholder="Поиск по браузеру"'));
assert(palette.includes('id="palIn"'));

const keyStart = html.indexOf("document.addEventListener('keydown', e => {\n  const mod = e.metaKey || e.ctrlKey");
const keyEnd = html.indexOf("\ndocument.addEventListener('visibilitychange'", keyStart);
assert(keyStart >= 0 && keyEnd > keyStart);
const shortcuts = html.slice(keyStart, keyEnd);
assert.match(shortcuts, /if \(K === 'KeyK'\) \{ e\.preventDefault\(\); if \(\$\('\.palette'\)\) closeModal\(\); else openPalette\(\); \}/,
  'Ctrl+K and Command+K remain routed through the shared modifier handler');
assert.match(html, /const kb = s => \{[\s\S]*?return s\.replace\(\/⌘\\s\+\/g, 'Ctrl\+'\)/,
  'the visible shortcut label still uses kb() for Windows');
const activeStyles = (html.match(/<style id="ux-macos-structure">([\s\S]*?)<\/style>/) || [])[1] || '';
assert.match(activeStyles, /\.sb-search span:not\(\.kbd\)\{min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap\}/,
  'the full search label ellipsizes cleanly in the narrow sidebar');

console.log('Browser-search wording, accessibility, responsive label, and shortcut tests passed');
