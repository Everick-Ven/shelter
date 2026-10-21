'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const bridge = read('src/shell_bridge.cc');
const shell = read('src/shell.cc');
const shellHeader = read('src/shell.h');
const ui = read('resources/ui/index.html');
const hostBridge = read('resources/ui/host-bridge.js');

const from = (text, start, end) => {
  const i = text.indexOf(start);
  assert.notEqual(i, -1, `missing source section: ${start}`);
  const j = text.indexOf(end, i + start.length);
  assert.notEqual(j, -1, `missing source boundary: ${end}`);
  return text.slice(i, j);
};

const navUrl = from(bridge, 'std::string NavUrl(',
                    '// ---- снимок вкладки через DevTools-протокол');
assert.match(navUrl, /lower == "about:blank"/);
assert.match(navUrl, /scheme != "http" && scheme != "https"/);
assert.match(navUrl, /CefParseURL\(CefString\(candidate\), parts\)/);
assert.match(navUrl, /CefString\(&parts\.host\)\.ToString\(\)\.empty\(\)/);
assert.match(navUrl, /candidate = "https:\/\/" \+ url/);
assert.match(navUrl, /return "about:blank"/);
assert.doesNotMatch(navUrl, /return has_scheme \? url/);

const viewOpen = from(bridge, 'if (m == "view.open")',
                      'if (m == "view.layout")');
assert.match(viewOpen, /ConsumeSelectedLocalFileUrl\(url\) \? url : NavUrl\(url\)/);
assert.equal((bridge.match(/NavUrl\(/g) || []).length, 2,
             'NavUrl must have exactly one call site');

const fileUrl = from(shell, 'std::string LocalFileUrl(', 'constexpr int kMinWidth');
assert.match(fileUrl, /fs::is_regular_file\(path, ec\)/);
assert.match(fileUrl, /prefix = "file:\/\/\/"/);
assert.match(fileUrl, /prefix = "file:\/\/"/);
assert.match(fileUrl, /encoded\.push_back\('%'\)/);
assert.match(fileUrl, /url_path\.rfind\("\/\/?", 0\)/);

const fileCallback = from(shell, 'class FileOpenCallback',
                           'void Shell::PickLocalFile()');
assert.match(fileCallback, /OnLocalFileSelected\(file_paths\[0\]\.ToString\(\)\)/);
const registerFile = from(shell, 'void Shell::OnLocalFileSelected(',
                           'bool Shell::ConsumeSelectedLocalFileUrl(');
assert.match(registerFile, /LocalFileUrl\(path\)/);
assert.match(registerFile, /selected_local_file_urls_\.insert\(url\)/);
assert.match(registerFile, /UiEvent\("file-open"/);
assert.match(registerFile, /size\(\) >= 16/);
const consumeFile = from(shell, 'bool Shell::ConsumeSelectedLocalFileUrl(',
                          'void Shell::ExtInstall(');
assert.match(consumeFile, /selected_local_file_urls_\.erase\(url\)/);
assert.match(shellHeader, /OnLocalFileSelected\(const std::string& path\)/);
assert.match(shellHeader, /ConsumeSelectedLocalFileUrl\(const std::string& url\)/);
assert.match(shellHeader, /selected_local_file_urls_/);

const event = from(ui, "case 'file-open':", "case 'context-close':");
assert.match(event, /openLocalFile\(d\.url\)/);
assert.doesNotMatch(event, /navigate\(d\.url\)/);
const openLocal = from(ui, 'function openLocalFile(', 'function openFileDlg(');
assert.match(openLocal, /\^file:/);
assert.match(openLocal, /loadUrl\(tab, localUrl\)/);
assert.doesNotMatch(openLocal, /resolveInput|navigate\(/);
const recordVisit = from(ui, 'function recordVisit(', '// Миграция: схлопнуть задвоенные');
assert.match(recordVisit, /\^file:/);
assert.match(hostBridge, /fileOpen: function \(\) \{ return q\('file\.open'\); \}/);

console.log('NavUrl scheme restrictions and selected-file flow checks passed');
