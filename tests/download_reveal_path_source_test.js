'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const paths = read('src/security_paths.h');
const bridge = read('src/shell_bridge.cc');
const nativeTest = read('tests/download_reveal_path_test.cc');

const selective = paths.slice(
  paths.indexOf('inline bool IsSymlinkOrJunction('),
  paths.indexOf('inline bool IsDirectoryWithoutSymlinkOrJunction(')
);
assert.match(selective, /FindFirstFileW\(path\.c_str\(\), &data\)/);
assert.match(selective, /IO_REPARSE_TAG_SYMLINK/);
assert.match(selective, /IO_REPARSE_TAG_MOUNT_POINT/);
assert.doesNotMatch(selective, /data\.dwReserved0 == IO_REPARSE_TAG_CLOUD/);

const revealGuard = bridge.slice(
  bridge.indexOf('const auto can_reveal_download ='),
  bridge.indexOf('if (m == "user-data.delete")')
);
assert.notEqual(revealGuard.indexOf('const auto can_reveal_download ='), -1);
assert.match(revealGuard, /IsRegularFileWithoutSymlinkOrJunctionChain\(normalized\)/);
assert.match(nativeTest, /fs::canonical\(fs::temp_directory_path\(\), ec\)/);
assert.match(nativeTest, /ordinary download is allowed/);
assert.match(nativeTest, /file symlink is rejected/);
assert.match(nativeTest, /junction in parent path is rejected/);
console.log('Download reveal path classification source checks passed');
