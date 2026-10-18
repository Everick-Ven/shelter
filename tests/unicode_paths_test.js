'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const pathUtils = read('src/path_utils.h');
assert.match(pathUtils, /std::filesystem::u8path\(value\)/,
  'UTF-8 path strings are converted to native filesystem paths');
assert.match(pathUtils, /path\.u8string\(\)/,
  'native filesystem paths are converted back to UTF-8');
assert.match(pathUtils, /__cpp_char8_t/,
  'path conversion supports both C++17 and char8_t standard libraries');

const scheme = read('src/ui_scheme.cc');
assert.match(scheme, /std::ifstream f\(path_utils::PathFromUtf8\(path\)/,
  'UI resource files are opened through UTF-8 filesystem paths');
const blocker = read('src/blocker.cc');
assert.match(blocker, /std::ifstream f\(path_utils::PathFromUtf8\(path\)/,
  'filter lists are opened through UTF-8 filesystem paths');
assert.match(blocker, /PathFromUtf8\(dir \+ "\/cosmetic\.css"\)/,
  'the cosmetic stylesheet is opened through a UTF-8 filesystem path');

const shell = read('src/shell.cc');
assert.match(shell, /PathFromUtf8\(platform::UiResourceDir\(\) \+ "\/icon-256\.png"\)/,
  'the app icon is loaded when the app directory contains Cyrillic');
assert.match(shell, /return path_utils::PathFromUtf8\(cache\) \/ "Extensions";/,
  'CEF cache paths are interpreted as UTF-8');
assert.match(shell, /PathFromUtf8\(platform::UserDataDir\(\)\)/,
  'the data directory is interpreted as UTF-8 before logging');
assert.doesNotMatch(shell, /fs::path\(platform::UserDataDir\(\)\)|fs::path\(cache\)/,
  'UTF-8 platform paths are not passed to locale-dependent narrow constructors');

const extensionEngine = read('src/extension_engine.h');
assert.match(extensionEngine, /PathFromUtf8\(path\)/,
  'extension mode files use UTF-8 file paths');
assert.match(extensionEngine, /directory_iterator it\(\s*path_utils::PathFromUtf8\(root\), ec\)/,
  'extension directories are enumerated through native Unicode paths');
assert.match(extensionEngine, /PathToUtf8\(dir\)/,
  'extension paths are returned as UTF-8 for CEF');
assert.doesNotMatch(extensionEngine, /\.string\(\)/,
  'filesystem paths are never converted with the locale-dependent string() API');

const browserApp = read('src/browser_app.cc');
assert.match(browserApp, /PathFromUtf8\(root\)/,
  'CEF user-data paths are validated using the native Unicode filesystem path');
assert.match(browserApp, /CefString\(&settings\.root_cache_path\) = root/,
  'CEF receives the user-data path as UTF-8');
const clients = read('src/clients.cc');
assert.match(clients, /PathFromUtf8\(downloads\)/,
  'download directory strings are converted from UTF-8 before filesystem use');
assert.match(clients, /PathToUtf8\(/,
  'download paths are returned to CEF as UTF-8');
const bridge = read('src/shell_bridge.cc');
assert.match(bridge, /PathFromUtf8\(raw_path\)/,
  'authorized reveal paths are converted from UTF-8 before filesystem checks');
const keyStore = read('src/secret_key.cc');
assert.match(keyStore, /PathFromUtf8\(UserDataDir\(\)\)/,
  'the protected master-key path supports a Unicode user-data directory');

const cmake = read('CMakeLists.txt');
assert.match(cmake, /add_executable\(ShelterUtf8PathTest/,
  'Windows and macOS CI compile a real Cyrillic-path filesystem test');
assert.match(cmake, /shelter-utf8-filesystem-path/,
  'the native Cyrillic-path test is registered with CTest');
const workflow = read('.github/workflows/build.yml');
assert.match(workflow, /Programs\\Проверка-SHELTER/,
  'Windows packaged acceptance runs from a Cyrillic directory');
assert.match(workflow, /Applications\/Проверка-SHELTER\.app/,
  'macOS packaged acceptance runs from a Cyrillic directory');
assert.match(workflow, /ShelterUtf8PathTest/,
  'the Windows and macOS workflows build the Unicode path test');

console.log('Cyrillic filesystem path regression checks passed');
