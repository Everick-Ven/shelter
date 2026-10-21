'use strict';

// Блокировщик рекламы должен отсекать рекламу ещё на этапе загрузки страницы.
// Пины закрывают найденные дыры, из-за которых оверлей-реклама (popunder,
// рекламный iframe) доходила до экрана:
//  1) владелец запроса считался по текущему фрейму, а не по странице верхнего
//     уровня — всё внутри рекламного iframe было «своим» и не блокировалось;
//  2) главный кадр пропускался всегда — скриптовый увод (location/meta refresh)
//     на рекламный адрес проходил без проверки;
//  3) рекламное окно создавалось по клику (popunder ловит жест пользователя);
//  4) фильтр молчал до прихода UI-настройки (g_enabled по умолчанию false);
//  5) косметика не внедрялась во фреймы с рекламным DOM.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf-8');

const blocker = read('src/blocker.cc');
const clients = read('src/clients.cc');
const shell = read('src/shell.cc');

const from = (text, start, end) => {
  const i = text.indexOf(start);
  assert.notEqual(i, -1, `нет исходного раздела: ${start}`);
  const j = text.indexOf(end, i + start.length);
  assert.notEqual(j, -1, `нет границы исходного раздела: ${end}`);
  return text.slice(i, j);
};

// --- 4) фильтр включён по умолчанию, правила грузятся по первому запросу ----
assert.match(blocker, /std::atomic<bool> g_enabled\{true\}/,
  'блокировщик включён по умолчанию: реклама режется до прихода UI-настройки');
const shouldBlock = from(blocker, 'bool ShouldBlock(', 'void RecordBlock(');
assert.match(shouldBlock, /if \(!g_loaded\.load\([^)]*\)\) LoadRules\(\);/,
  'правила грузятся по первому запросу, а не по первому тумблеру');
assert.ok(shouldBlock.indexOf('g_enabled.load') < shouldBlock.indexOf('LoadRules()'),
  'выключенный фильтр не тратит время на загрузку правил');
const cosmeticCss = from(blocker, 'const std::string& CosmeticCss()',
                         '}  // namespace blocker');
assert.match(cosmeticCss, /if \(!g_loaded\.load\([^)]*\)\) LoadRules\(\);/,
  'косметика тоже берёт актуальные правила без тумблера');

// --- 1)+2) общий контур сетевой фильтрации ----------------------------------
const check = from(clients, 'cef_return_value_t BlockerCheck(',
                   '// HTTPS-only');
assert.match(check, /browser->GetMainFrame\(\)->GetURL\(\)/,
  'владелец запроса — страница верхнего уровня, а не текущий фрейм');
assert.ok(check.indexOf('browser->GetMainFrame()') <
          check.indexOf('ShouldBlock(url, page'),
  'URL главного кадра подставляется в ShouldBlock до проверки');
assert.match(check, /TT_CLIENT_REDIRECT_FLAG/,
  'скриптовые уводы главного кадра на рекламные адреса отсекаются');
assert.match(check, /RT_MAIN_FRAME &&\s*\n\s*\(request->GetTransitionType\(\) & TT_CLIENT_REDIRECT_FLAG\) == 0/,
  'главный кадр без скриптового увода грузится: пользователь пришёл сам');
assert.ok(!/RT_MAIN_FRAME\) return RV_CONTINUE/.test(check),
  'главный кадр больше не пропускается безусловно');

// --- 3) рекламное окно не создаётся вообще ---------------------------------
const tabPopup = from(clients, 'bool TabClient::OnBeforePopup(',
                      'bool TabClient::OnOpenURLFromTab(');
assert.ok(tabPopup.indexOf('::shelter::blocker::ShouldBlock(url') <
          tabPopup.indexOf('client = new PopupClient()'),
  'URL всплывающего окна проверяется до создания окна');
assert.match(tabPopup, /::shelter::blocker::RecordBlock\(/,
  'отсечённое окно попадает в статистику блокировок');
assert.match(tabPopup, /OnTabNewWindow\(browser, url, false\)/,
  'пользователь получает тост о заблокированном окне с возможностью открыть');
const popAd = from(tabPopup, '::shelter::blocker::ShouldBlock(url',
                   '// window.open(');
assert.match(popAd, /return true;/,
  'рекламное окно отменяется целиком, а не перенаправляется во вкладку');

// --- 5) косметика во всех фреймах ------------------------------------------
const inject = from(shell, 'static void InjectBlockerCss(',
                    'void Shell::PushProtections(');
assert.ok(!inject.includes('IsMain'),
  'стиль внедряется и во фреймы с рекламным DOM, не только в главный');

// --- списки правил: пути и сети, обслуживающие оверлей-рекламу -------------
const patterns = read('resources/filters/url-patterns.txt');
for (const needle of ['/adv/', '/vast', '/adframe', '/tizer', '/banners/',
                      '/reklam', 'clickunder', '/pop-under', '/popunder/']) {
  assert.ok(patterns.includes(needle), `шаблон рекламного пути в списке: ${needle}`);
}
const domains = read('resources/filters/tracker-domains.txt');
for (const needle of ['popcash.net', 'hybrid.ai', 'aniview.com',
                      'tsyndicate.com', 'moevideo.net', 'adsterra.com',
                      'propellerads.com']) {
  assert.ok(domains.includes(needle), `рекламная сеть в списке: ${needle}`);
}
const cosmetic = read('resources/filters/cosmetic.css');
for (const needle of ['.popup-ad', '.clickunder-layer', 'iframe[src*="popcash"]',
                      '#rek']) {
  assert.ok(cosmetic.includes(needle), `косметическое правило в списке: ${needle}`);
}

console.log('blocker load-cut hardening: network base, popup cut, defaults, cosmetic, lists passed');
