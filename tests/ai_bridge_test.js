'use strict';

// Мост внешнего ИИ-агента: проверка подключения и чат обязаны идти через
// нативный HTTPS, а не фейковый {"ok":true} и не сеть страницы (CSP остаётся
// строгим, ключ не покидает браузерный процесс кроме TLS к провайдеру).

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const read = name => fs.readFileSync(path.join(root, name), 'utf8');

const bridge = read('src/shell_bridge.cc');
const start = bridge.indexOf('bool Shell::HandleBridge(');
const end = bridge.indexOf('\nvoid Shell::TabAction(', start);
assert(start >= 0 && end > start, 'Shell::HandleBridge must be present');
const handler = bridge.slice(start, end);

// Методы зарегистрированы в авторизованном мосту (единая проверка IsUiBrowser
// выше покрывает и их — см. bridge_authorization_test.js).
assert(handler.includes('m == "ai.test"'), 'ai.test must be handled by the bridge');
assert(handler.includes('m == "ai.chat"'), 'ai.chat must be handled by the bridge');

// Нативный HTTPS через CefURLRequest; ключ — в Authorization: Bearer.
assert(bridge.includes('class AiUrlRequestClient : public CefURLRequestClient'),
  'AI requests must run on a native CefURLRequest client');
assert(bridge.includes('CefURLRequest::Create'), 'AI requests must use native CefURLRequest');
assert(bridge.includes('Bearer '), 'provider API key must use the Bearer scheme');
// OpenAI-совместимые пути.
assert(bridge.includes('"models"'), 'ai.test must probe the /models endpoint');
assert(bridge.includes('"chat/completions"'), 'ai.chat must post to /chat/completions');
assert(bridge.includes('chat ? "chat/completions" : "models"'),
  'test and chat must share one URL builder');
// Только https наружу; http — лишь loopback для локальных моделей.
assert(bridge.includes('IsLoopbackHost'), 'plain http must stay loopback-only');
for (const host of ['"localhost"', '"127.0.0.1"', '"::1"']) {
  assert(bridge.includes(host), `loopback allow-list must contain ${host}`);
}
// userinfo в baseUrl запрещён: иначе ключ уйдёт чужому хосту.
assert(bridge.includes("authority.find('@')"), 'userinfo in baseUrl must be rejected');
// Лимиты ответов и разбор вердикта.
assert(bridge.includes('kAiTestMaxBytes') && bridge.includes('kAiChatMaxBytes'),
  'provider responses must be size-capped');
assert(bridge.includes('response_too_large'), 'oversized provider replies must fail loudly');
assert(bridge.includes('unknown_model'),
  'connect must flag a model missing from the provider list');
assert(bridge.includes('choices') && bridge.includes('message'),
  'chat must unpack choices[0].message.content');
// Текст причины возвращаем через Success: host-bridge на onFailure отдаёт null.
assert(bridge.includes('{\\"ok\\":false,\\"error\\":'),
  'AI failures must carry a machine-readable error');

// Легаси-протокол инлайн-адаптера ведёт туда же, а не в молчаливый {"ok":true}.
const clients = read('src/clients.cc');
assert(clients.includes('cmd == "ai:test"'),
  'legacy inline-adapter ai:test must route to the native bridge');
assert(clients.includes('cmd == "ai:chat"'),
  'legacy inline-adapter ai:chat must route to the native bridge');
assert.match(clients, /cmd == "ai:test"[\s\S]{0,600}HandleBridge\(browser, method, d, callback\)/,
  'legacy AI commands must reuse the authorized HandleBridge methods');

// host-bridge отдаёт вёрстке оба метода (иначе диалог показывает «Нет доступа»).
const hostBridge = read('resources/ui/host-bridge.js');
assert.match(hostBridge, /testAgent:\s*function\s*\(cfg\)/,
  'host-bridge must expose testAgent to the page');
assert(hostBridge.includes("q('ai.test'"), 'testAgent must call ai.test');
assert.match(hostBridge, /agentChat:\s*function\s*\(cfg,\s*prompt\)/,
  'host-bridge must expose agentChat to the page');
assert(hostBridge.includes("q('ai.chat'"), 'agentChat must call ai.chat');

// Вёрстка пользуется нативными методами и показывает их вердикт.
const ui = read('resources/ui/index.html');
assert(ui.includes('NAT.testAgent(cfg)'), 'connect dialog must use the native testAgent');
assert(ui.includes('NAT.agentChat(S.ai, q)'), 'chat must use the native agentChat');
assert.match(ui, /r\.ok && r\.text/, 'chat must render provider text from the native reply');

console.log('AI agent bridge tests passed');
