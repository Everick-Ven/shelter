'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const sourceRoots = ['src', 'resources', 'tests'];
const sourceExtensions = new Set(['.cc', '.h', '.mm', '.js', '.html']);
let checked = 0;

function walk(directory) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      walk(file);
    } else if (entry.isFile() && sourceExtensions.has(path.extname(file)) &&
               path.resolve(file) !== path.resolve(__filename)) {
      const source = fs.readFileSync(file, 'utf8');
      assert.doesNotMatch(source, /\bOpenExternal\s*\(/,
                          `stale OpenExternal declaration or call in ${path.relative(root, file)}`);
      checked++;
    }
  }
}

for (const directory of sourceRoots) walk(path.join(root, directory));
assert.ok(checked > 0, 'source scan did not inspect any files');
console.log(`OpenExternal dead-code removal check passed (${checked} source files)`);
