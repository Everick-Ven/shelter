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

console.log('UI assistant cleanup tests passed');
