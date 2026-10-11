'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const cmake = read('CMakeLists.txt');
assert.match(cmake, /^set\(SHELTER_VERSION "\d+\.\d+\.\d+"\)$/m);
assert.match(cmake, /configure_file\(cmake\/app_version\.h\.in/);
assert.match(cmake, /configure_file\(cmake\/app_version\.txt\.in/);
assert.match(cmake, /configure_file\(win\/shelter\.rc\.in/);
assert.match(read('cmake/app_version.h.in'), /"@SHELTER_VERSION@"/);
assert.match(read('cmake/app_version.txt.in'), /^@SHELTER_VERSION@\s*$/);
assert.match(read('win/shelter.rc.in'), /FILEVERSION @SHELTER_FILE_VERSION_RC@/);
assert.match(read('win/shelter.rc.in'), /"FileVersion", "@SHELTER_VERSION@"/);
assert.match(read('src/common.h'), /#include "src\/app_version\.h"/);
assert.match(read('src/ui_scheme.cc'), /ReplaceAll\(&data, "__APP_VERSION__", kAppVersion\)/);
assert.match(read('resources/ui/host-bridge.js'), /var VERSION = '__APP_VERSION__'/);
assert.match(read('resources/ui/index.html'), /window\.shelterNative && window\.shelterNative\.version/);

const installer = read('installer/shelter.iss');
assert.match(installer, /AppVersion=\{#AppVersion\}/);
assert.doesNotMatch(installer, /AppVersion=\{#\d/);
const workflow = read('.github/workflows/build.yml');
assert.match(workflow, /build\/shelter-version\.txt/);
assert.match(workflow, /"\/DAppVersion=\$version"/);
assert.match(workflow, /SHELTER-macos-x64-\$\{version\}\.dmg/);
assert.match(workflow, /--expected-version/);
assert.doesNotMatch(workflow, /1\.0\.165/);
assert.match(read('README.md'), /SHELTER_VERSION/);
assert.match(read('README.md'), /SHELTER-macos-x64-<версия>\.dmg/);

console.log('single-source application version propagation test passed');
