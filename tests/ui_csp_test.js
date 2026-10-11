'use strict';

const assert = require('assert');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const uiDir = path.join(__dirname, '..', 'resources', 'ui');
const html = fs.readFileSync(path.join(uiDir, 'index.html'), 'utf8');
const bridge = fs.readFileSync(path.join(uiDir, 'host-bridge.js'), 'utf8');
const csp = html.match(/<meta\s+http-equiv="Content-Security-Policy"\s+content="([^"]*)"/i);
assert(csp, 'CSP meta tag is required');
const scriptSrc = csp[1].split(';').map(x => x.trim()).find(x => /^script-src\s/.test(x));
assert(scriptSrc, 'script-src directive is required');
assert(!/['"]unsafe-inline['"]/.test(scriptSrc), 'script-src must not allow unsafe-inline');

const hashes = Array.from(scriptSrc.matchAll(/'sha256-([A-Za-z0-9+/]+={0,2})'/g), m => m[1]);
const inlineScripts = Array.from(html.matchAll(/<script\b(?![^>]*\bsrc\s*=)[^>]*>([\s\S]*?)<\/script\s*>/gi), m => m[1]);
assert.strictEqual(inlineScripts.length, 5, 'expected the five existing inline scripts');
assert.strictEqual(hashes.length, inlineScripts.length, 'one CSP hash is required per inline script');
const expected = inlineScripts.map(script => crypto.createHash('sha256').update(script, 'utf8').digest('base64')).sort();
assert.deepStrictEqual(hashes.slice().sort(), expected, 'CSP script hashes must match the exact inline script bytes');

const markupOnly = html
  .replace(/<script\b[^>]*>[\s\S]*?<\/script\s*>/gi, '')
  .replace(/<style\b[^>]*>[\s\S]*?<\/style\s*>/gi, '');
assert(!/<[a-z][^>]*\son[a-z]+\s*=/i.test(markupOnly), 'HTML event-handler attributes are forbidden');
assert(!/\b(?:SECRET_KEY|__SECRET_KEY__)\b/.test(bridge), 'the renderer bridge must not contain the master key');
assert(!/function\s+(?:chacha20|hmac|sha256)\s*\(/i.test(bridge), 'secret crypto must not execute in renderer JavaScript');

console.log('CSP hashes, inline-handler removal, and renderer secret isolation tests passed');
