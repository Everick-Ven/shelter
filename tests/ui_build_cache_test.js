'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const cmake = read('CMakeLists.txt');
const cefDownload = read('cmake/DownloadCEF.cmake');
assert.match(cmake, /^set\(SHELTER_VERSION "\d+\.\d+\.\d+"\)$/m);
assert.match(cmake, /^set\(CEF_ARCHIVE_SHA256_WINDOWS64 "[0-9a-f]{64}"\)$/m);
assert.match(cmake, /^set\(CEF_ARCHIVE_SHA256_MACOSX64 "[0-9a-f]{64}"\)$/m);
const pinnedCefVersion = cmake.match(/^set\(CEF_VERSION "([^"]+)"\)$/m);
const cacheKeyVersion = read('.github/workflows/build.yml').match(/^  CEF_VERSION_KEY: cef-(\d+\.\d+\.\d+)$/m);
assert(pinnedCefVersion && cacheKeyVersion);
assert.strictEqual(cacheKeyVersion[1], pinnedCefVersion[1].match(/^\d+\.\d+\.\d+/)[0], 'the CEF cache key follows the exact pinned binary version');
assert.match(cmake, /DownloadCEF\("\$\{CEF_PLATFORM\}" "\$\{CEF_CHANNEL\}" "\$\{CEF_VERSION\}" "\$\{CMAKE_SOURCE_DIR\}\/third_party\/cef" "\$\{CEF_ARCHIVE_SHA256\}"\)/);
assert.match(cefDownload, /EXPECTED_HASH "SHA256=\$\{expected_sha256\}"/);
assert.match(cefDownload, /file\(SHA256 "\$\{CEF_DOWNLOAD_PATH\}" CEF_ACTUAL_SHA256\)/);
assert.match(cefDownload, /\.shelter-cef-archive-sha256/);
assert.match(cefDownload, /-E tar xjf/);
assert.match(cefDownload, /CEF_DOWNLOAD_FILENAME "\$\{CEF_DISTRIBUTION\}\.tar\.bz2"/);
assert.match(cefDownload, /CACHE INTERNAL "CEF_ROOT" FORCE/);
assert.doesNotMatch(cefDownload, /\.sha1|SHA1=/, 'CEF download must not trust an unpinned SHA-1 sidecar');
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

const shell = read('src/shell.cc');
const uiCreated = shell.slice(
  shell.indexOf('void Shell::OnUiCreated'),
  shell.indexOf('void Shell::OnUiClosed'));
assert.match(read('src/shell.h'), /bool ui_http_cache_cleared_ = false;/);
assert.match(uiCreated, /if \(browser && !ui_http_cache_cleared_\)/);
assert.match(uiCreated, /ExecuteDevToolsMethod\(\s*0, "Network\.clearBrowserCache", nullptr\)/);
assert.doesNotMatch(uiCreated, /clearBrowserCookies|Storage\.clear|localStorage|IndexedDB/i,
                    'startup cache cleanup must not remove persistent browser data');

for (const file of ['src/clients.cc', 'src/renderer_app.cc']) {
  assert.match(read(file), /url\.rfind\(kUiOriginPrefix, 0\) == 0/,
               `${file} must continue checking the stable UI origin`);
}

console.log('versioned UI and host bridge URL regression test passed');
