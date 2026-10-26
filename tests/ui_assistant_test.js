'use strict';

// Вкладка «Ассистент»: без подписей LOCAL SHIELD, без кнопки поиска,
// свои сообщения копируются, локального движка нет (только ошибка
// недоступности), затравки «Обзор возможностей» нет (пустой «Новый
// диалог»), кнопка создания — «Новый чат».
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'resources', 'ui', 'index.html'), 'utf8');

const has = (needle, label) =>
  assert.ok(html.includes(needle), `нет якоря: ${label}`);
const no = (needle, label) =>
  assert.ok(!html.includes(needle), `запрещённый якорь: ${label}`);
const count = needle => html.split(needle).length - 1;

// 1. Подписи удалены.
no('LOCAL SHIELD', 'плашка LOCAL SHIELD');
no('dt-local-label', 'класс плашки LOCAL SHIELD');
no('Локальный AI-ассистент браузера', 'подпись welcome-экрана');
has('<h1>Чем помочь сегодня?</h1>', 'заголовок welcome-экрана остался');

// 2. Кнопки «Искать в …» нет ни в тулзах ответа, ни в действиях API-ответа.
no('Искать в ', 'кнопка поиска из ответов');
const toolsStart = html.indexOf('<div class="dt-answer-tools">');
const toolsEnd = html.indexOf('function dtConversationHtml()');
assert.ok(toolsStart >= 0 && toolsEnd > toolsStart, 'секция тулзов ответа найдена');
assert.ok(!html.slice(toolsStart, toolsEnd).includes('searchIn'), 'тулзы ответа без поиска');
has('data-act="dtCopy"', 'копирование ответа осталось');
has('data-act="dtRetry"', 'перегенерация ответа осталась');

// 3. Свои сообщения копируются.
has('dt-user-copy', 'кнопка копирования своего сообщения');
has('data-toast="Сообщение скопировано"', 'тост копирования своего сообщения');
has('.dt-user-copy{opacity:.55}', 'стиль кнопки копирования');
has("toast(el.dataset.toast || 'Ответ скопирован'", 'dtCopy поддерживает свой текст тоста');

// 4. Локального движка нет: везде только ошибка недоступности.
no('dtBuildLocalReply', 'локальный движок ответов');
no('DT_SUGGESTIONS', 'мёртвые подсказки локального движка');
has('function dtOfflineReply()', 'единый ответ-ошибка');
has("mode: 'Нет связи'", 'режим ошибки');
has('Ассистент недоступен в данный момент', 'текст ошибки недоступности');
has('Попробуйте ещё раз или чуть позже', 'просьба попробовать позже');
assert.strictEqual(count('finishWith(dtOfflineReply())'), 3, 'ошибка на всех трёх путях: ошибка API, исключение, нет API');

// 5. Затравки «Обзор возможностей» нет; пусто = окно «Новый диалог».
no('dtSeedThreads', 'затравка диалогов');
no('Обзор возможностей SHELTER AI', 'затравленный диалог');
no('Что умеет встроенный Ассистент', 'затравленный вопрос');
has('function dtBlankThread()', 'пустой диалог');
has("title: 'Новый диалог'", 'заголовок пустого диалога');
assert.strictEqual(count('[dtBlankThread()]'), 2, 'пустой диалог при старте и при удалении последнего');
has("th.id === 'dt-welcome'", 'чистка нетронутой затравки у существующих пользователей');

// 6. Кнопка создания — «Новый чат».
has('<span>Новый чат</span>', 'подпись кнопки создания');
has('data-tip="Новый чат"', 'подсказка кнопки создания');

// Совместимость с разбором задач Агентом (Stage D).
has("function dtNewThread(initialPrompt = '', opts = {})", 'сигнатура dtNewThread не менялась');

// The Assistant must not expose decorative privacy shields or repeat storage/privacy claims.
for (const [needle, label] of [
  ['dt-private', 'rail shield indicator'],
  ['dt-privacy-pill', 'header privacy pill'],
  ['dt-local-label', 'LOCAL SHIELD styling hook'],
  ['dt-local-dot', 'decorative local status dot'],
  ['Локально · без облака', 'redundant local/privacy status caption'],
  ['LOCAL SHIELD', 'LOCAL SHIELD caption'],
  ['Локальная защита контекста', 'context-protection tooltip'],
  ['Приватный контур', 'privacy pill caption'],
  ['Переписка и ключи хранятся исключительно на вашем устройстве', 'composer privacy footer'],
]) no(needle, label);

const renderStart = html.indexOf('RENDER.deepthink = m => {');
const renderEnd = html.indexOf('\n};', renderStart);
assert.ok(renderStart >= 0 && renderEnd > renderStart, 'active Assistant renderer is present');
const assistantRender = html.slice(renderStart, renderEnd);
has('id="dtTopSub"', 'existing Assistant subtitle remains present');
assert.ok(assistantRender.includes("'Локальный контур SHELTER · 0 байт во внешнюю сеть'"),
  '#dtTopSub local-mode copy remains unchanged pending approval');
assert.ok(assistantRender.includes("'Подключён по API · ключи на устройстве'"),
  '#dtTopSub API-mode copy remains unchanged pending approval');

const sendRowStart = assistantRender.indexOf('<div class="omni-row">');
const sendRowEnd = assistantRender.indexOf('</div>', sendRowStart);
assert.ok(sendRowStart >= 0 && sendRowEnd > sendRowStart, 'bottom composer row is present');
const sendRow = assistantRender.slice(sendRowStart, sendRowEnd);
assert.ok(sendRow.includes('data-act="dtSend"'), 'send button stays inside the lower composer row');
const footStart = assistantRender.indexOf('<div class="dt-composer-foot">');
const footEnd = assistantRender.indexOf('</div>', footStart);
assert.ok(footStart >= 0 && footEnd > footStart, 'composer helper row is present');
const composerFoot = assistantRender.slice(footStart, footEnd);
assert.ok(composerFoot.includes('Enter — отправить · Shift+Enter — перенос строки'), 'keyboard help is preserved');
assert.ok(!composerFoot.includes('shield-check') && !composerFoot.includes('хранятся'), 'composer helper has no privacy badge or copy');

const assistantStyles = [...html.matchAll(/<style[^>]*>([\s\S]*?)<\/style>/g)].map(match => match[1]).join('\n');
assert.ok(!assistantStyles.includes('.dt-private') && !assistantStyles.includes('.dt-privacy-pill') &&
  !assistantStyles.includes('.dt-local-label'), 'privacy-indicator styling is removed');
assert.ok(assistantStyles.includes('.dt-omni .omni-row>.sp{display:block;flex:1;min-width:8px}'),
  'the bottom-row spacer keeps the send button on the right');
assert.ok(assistantStyles.includes('.dt-omni .omni-row .send{position:relative;align-self:center}'),
  'send stays in normal flow, vertically centered in the stable bottom row');

const patchStart = html.indexOf('function patchAssistant() {');
const patchEnd = html.indexOf('let scheduled = false;', patchStart);
assert.ok(patchStart >= 0 && patchEnd > patchStart, 'Assistant patcher is present');
const patch = html.slice(patchStart, patchEnd);
assert.ok(patch.includes("app.querySelector('.dt-rail .dt-new')"), 'New Chat still moves into the right-side actions');
assert.ok(!patch.includes('dt-privacy-pill') && !patch.includes('mac-send') && !patch.includes('insertBefore(send'),
  'patchAssistant neither recreates privacy indicators nor moves the send button');
assert.ok(!html.includes("DT_PATCH_SEL = '.dt-app,.dt-topbar,.dt-top-actions,.dt-privacy-pill"),
  'the DOM patch observer does not watch a removed privacy pill');

console.log('UI assistant cleanup tests passed');
