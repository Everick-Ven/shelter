'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const cmake = read('CMakeLists.txt');
const workflow = read('.github/workflows/build.yml');
const browser = read('src/browser_app.cc');
const shell = read('src/shell.cc');
const bridge = read('src/shell_bridge.cc');
const platformHeader = read('src/platform.h');
const win = read('src/platform_win.cc');
const mac = read('src/platform_mac.mm');
const checker = read('tools/check_ci_hooks.py');
const ui = read('resources/ui/index.html');
const hostBridge = read('resources/ui/host-bridge.js');
const geometry = read('tools/check_geometry.py');

const from = (text, start, end) => {
  const i = text.indexOf(start);
  assert.notEqual(i, -1, `missing source section: ${start}`);
  const j = text.indexOf(end, i + start.length);
  assert.notEqual(j, -1, `missing source boundary: ${end}`);
  return text.slice(i, j);
};

assert.match(cmake, /option\(SHELTER_CI_HOOKS[^\n]*OFF\)/);
assert.match(cmake, /if\(SHELTER_CI_HOOKS\)[\s\S]*target_compile_definitions\(\$\{CEF_TARGET\} PRIVATE SHELTER_CI_HOOKS=1\)/);

const fillSettings = from(browser, 'void FillSettings(', '\n}\n\n}  // namespace shelter');
assert.match(fillSettings, /#if defined\(SHELTER_CI_HOOKS\)[\s\S]*getenv\("SHELTER_DIAG"\)[\s\S]*#else[\s\S]*LOGSEVERITY_DISABLE[\s\S]*#endif/);
const createUi = from(shell, 'CefRefPtr<CefBrowserView> Shell::CreateUiView(',
                      'void Shell::WindowMiniaturized(');
assert.match(createUi, /#if defined\(SHELTER_CI_HOOKS\)[\s\S]*getenv\("SHELTER_SMOKE_URL"\)[\s\S]*#endif/);
const debugBridge = from(bridge, '#if defined(SHELTER_CI_HOOKS)',
                         '#endif  // SHELTER_CI_HOOKS');
for (const hook of ['dbg.hit', 'dbg.clip']) assert.ok(debugBridge.includes(hook));
const bridgeGuard = bridge.indexOf('#if defined(SHELTER_CI_HOOKS)');
assert.notEqual(bridgeGuard, -1);
assert.doesNotMatch(bridge.slice(0, bridgeGuard), /dbg\.(?:hit|clip)/);
assert.match(platformHeader, /#if defined\(SHELTER_CI_HOOKS\)[\s\S]*SetClipLevel[\s\S]*DumpWindowChain[\s\S]*#endif/);
assert.match(platformHeader, /#if defined\(SHELTER_CI_HOOKS\)[\s\S]*DebugHitTest[\s\S]*#endif/);

function assertHookGuard(source, symbol, label) {
  let at = -1;
  let count = 0;
  while ((at = source.indexOf(symbol, at + 1)) !== -1) {
    count++;
    const start = source.lastIndexOf('#if defined(SHELTER_CI_HOOKS)', at);
    const end = source.indexOf('#endif', at);
    assert.ok(start !== -1 && end > at,
              `${label} must be enclosed by SHELTER_CI_HOOKS`);
  }
  assert.ok(count > 0, `${label} is missing`);
}
function assertPreprocessorBalanced(source, label) {
  const stack = [];
  for (const [lineNumber, line] of source.split(/\r?\n/).entries()) {
    const directive = line.match(/^\s*#\s*(if|ifdef|ifndef|endif)\b/);
    if (!directive) continue;
    if (directive[1] === 'endif') {
      assert.ok(stack.length > 0, `${label}:${lineNumber + 1}: stray #endif`);
      stack.pop();
    } else {
      stack.push(lineNumber + 1);
    }
  }
  assert.equal(stack.length, 0,
               `${label}: unterminated preprocessor guard at ${stack.join(', ')}`);
}
for (const [name, source] of [['Windows', win], ['macOS', mac]]) {
  assertHookGuard(source, 'DebugHitTest(', `${name} DebugHitTest`);
  assertHookGuard(source, 'SetClipLevel(', `${name} SetClipLevel`);
  assertHookGuard(source, 'DumpWindowChain(', `${name} DumpWindowChain`);
  assertPreprocessorBalanced(source, `${name} platform source`);
}
assertHookGuard(mac, 'g_forced_key', 'macOS forced-key counter');
assertHookGuard(mac, 'g_redirected', 'macOS redirected-event counter');
assertHookGuard(mac, 'DumpView(', 'macOS diagnostic view dump');
for (const [file, source] of [['src/browser_app.cc', browser], ['src/shell.cc', shell],
                              ['src/shell_bridge.cc', bridge], ['src/platform.h', platformHeader]]) {
  assertPreprocessorBalanced(source, file);
}

assert.match(workflow, /-DSHELTER_CI_HOOKS=OFF/);
assert.match(workflow, /-DSHELTER_CI_HOOKS=ON/);
assert.match(workflow, /check_ci_hooks\.py build off/);
assert.match(workflow, /check_ci_hooks\.py build-ci on/);
assert.match(workflow, /check_geometry\.py build-ci/);
assert.doesNotMatch(hostBridge, /window\.cefQuery\s*=/,
                    'CI timing must not overwrite the native CEF V8 binding');
assert.match(hostBridge, /window\.__shCefQuery\s*=\s*bridgeCefQuery/);
assert.match(hostBridge, /bridgeCefQuery\.call\(window/);
assert.match(ui, /window\.__shCefQuery/);
assert.match(ui, /query\.call\(window/);
const smokeStart = ui.indexOf("if (!/[?&#]smoke=site\\b/.test(location.href)) return;");
assert.notEqual(smokeStart, -1, 'the geometry smoke URL must activate its native-view check');
const smokeListenerStart = ui.lastIndexOf("document.addEventListener('DOMContentLoaded', () => {", smokeStart);
const smokeEnd = ui.indexOf("  }, { once: true });", smokeStart);
assert.ok(smokeListenerStart >= 0 && smokeEnd > smokeStart, 'geometry smoke must run after DOM initialization');
const smokeBootstrap = ui.slice(smokeStart, smokeEnd);
assert.match(smokeBootstrap, /window\.shelter\.navigate\(target\)/, 'geometry smoke navigates the active UI tab to a real site');
assert.match(smokeBootstrap, /window\.shelterOpen\(target, \{ reveal: true \}\)/, 'geometry smoke retries the native view bridge until the viewport is ready');
assert.match(smokeBootstrap, /attempts < 60/);
assert.match(workflow, /acceptance_smoke\.py[^\n]*build-ci/);
assert.match(workflow, /check_ci_hooks\.py "\$dir" off --binary-only/);
assert.match(workflow, /check_ci_hooks\.py "\$portableCheck" off --binary-only/);
assert.match(workflow, /check_ci_hooks\.py "\$app_dir" off --binary-only/);
assert.match(workflow, /app_dir="\$HOME\/Applications\/Проверка-SHELTER\.app"/);
assert.match(workflow, /Remove-Item -LiteralPath \$profile -Recurse -Force/);
assert.match(workflow, /rm -rf "\$HOME\/Library\/Application Support\/SHELTER"/);
assert.match(workflow, /SourceDir=.*build\\Release/);
assert.match(workflow, /make_dmg\.sh build\/Release/);
assert.match(checker, /SHELTER_SMOKE_URL/);
assert.match(geometry, /--remote-debugging-port=/);
assert.match(geometry, /--remote-debugging-address=127\.0\.0\.1/);
assert.match(geometry, /import websocket/);
assert.match(geometry, /window\.shelter\.navigate\('https:\/\/example\.com\/'\)/);
assert.match(geometry, /shelterCefRequest\('view\.open'/);
assert.match(geometry, /persist:space-/);
assert.match(geometry, /native_open_attempts/);
assert.match(geometry, /shell: content bounds/);
const geometrySteps = workflow.match(/- name: Geometry acceptance \(CI hooks\)[\s\S]*?(?=\n      - name: )/g) || [];
assert.equal(geometrySteps.length, 2, 'geometry acceptance must run on both target platforms');
for (const step of geometrySteps) {
  assert.match(step, /check_geometry\.py/);
  assert.match(step, /websocket-client/);
}
assert.doesNotMatch(checker, /dbg\.protections/);

const textExtensions = new Set(['.cc', '.h', '.mm', '.js', '.html', '.py', '.yml', '.yaml', '.cmake', '.sh', '.iss', '.txt']);
const forbiddenSwitch = new RegExp(['--remote', 'debugging', 'port'].join('-'));
const switchMatches = [];
function scan(directory) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    if (['.git', 'third_party', 'build', 'build-ci', 'node_modules', '__pycache__'].includes(entry.name)) continue;
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) scan(file);
    else if (entry.isFile() && textExtensions.has(path.extname(file)) && path.resolve(file) !== path.resolve(__filename)) {
      const text = fs.readFileSync(file, 'utf8');
      if (forbiddenSwitch.test(text)) switchMatches.push(path.relative(root, file));
    }
  }
}
scan(root);
assert.ok(switchMatches.length > 0, 'expected smoke tools to use a remote debugging port');
assert.ok(switchMatches.every(file => file.startsWith(`tools${path.sep}`) && file.endsWith('.py')),
          `remote debugging port must be confined to tools/*.py: ${switchMatches.join(', ')}`);

console.log('CI-only hook gating, release/CI build separation, and remote-debugging source checks passed');
