'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const cmake = read('CMakeLists.txt');
assert.match(cmake, /^set\(SHELTER_VERSION "\d+\.\d+\.\d+"\)$/m);
assert.match(cmake, /string\(TIMESTAMP SHELTER_BUILD_TIMESTAMP/);
assert.match(cmake, /string\(RANDOM LENGTH 12 ALPHABET 0123456789abcdef SHELTER_BUILD_NONCE\)/);
assert.match(cmake, /set\(SHELTER_BUILD_ID "\$\{SHELTER_BUILD_TIMESTAMP\}-\$\{SHELTER_BUILD_NONCE\}"\)/);
assert.match(read('cmake/app_version.h.in'), /kAppVersion\[\] = "@SHELTER_VERSION@"/);
assert.match(read('cmake/app_version.h.in'), /kAppBuildId\[\] = "@SHELTER_BUILD_ID@"/);

const common = read('src/common.h');
assert.match(common, /index\.html\?v=\"\) \+ kAppVersion \+ "-"/);
assert.match(common, /kAppBuildId/);
assert.match(common, /kUiOriginPrefix\[\] = "shelter:\/\/app\/"/);

const scheme = read('src/ui_scheme.cc');
assert.match(scheme, /host-bridge\.js\?v=" \+ std::string\(kAppVersion\) \+ "-"/);
assert.match(scheme, /kAppBuildId/);
assert.match(scheme, /url\.rfind\(kUiOriginPrefix, 0\) != 0/);
const queryStrip = scheme.indexOf('rel.find_first_of("?#")');
const pathSanitize = scheme.indexOf('SanitizeRelPath(&rel)');
assert(queryStrip >= 0 && pathSanitize > queryStrip,
       'query/hash must be removed before sanitizing the relative UI path');
assert.match(scheme, /Cache-Control", "no-store"/);

for (const file of ['src/clients.cc', 'src/renderer_app.cc']) {
  assert.match(read(file), /url\.rfind\(kUiOriginPrefix, 0\) == 0/,
               `${file} must continue checking the stable UI origin`);
}

console.log('versioned UI and host bridge URL regression test passed');
