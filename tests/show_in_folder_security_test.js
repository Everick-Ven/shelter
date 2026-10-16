'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const shellBridge = read('src/shell_bridge.cc');
const shell = read('src/shell.cc');
const shellHeader = read('src/shell.h');
const paths = read('src/security_paths.h');
const pathTest = read('tests/security_paths_test.cc');
const win = read('src/platform_win.cc');
const mac = read('src/platform_mac.mm');
const hostBridge = read('resources/ui/host-bridge.js');
const ui = read('resources/ui/index.html');

const from = (text, start, end) => {
  const i = text.indexOf(start);
  assert.notEqual(i, -1, `нет исходного раздела: ${start}`);
  const j = text.indexOf(end, i + start.length);
  assert.notEqual(j, -1, `нет границы исходного раздела: ${end}`);
  return text.slice(i, j);
};

// Путь проверяется только после авторизации UI и до вызова платформенного API.
const bridge = from(shellBridge, 'bool Shell::HandleBridge(',
                    '// ---- действия над вкладкой');
assert.match(bridge, /if \(!IsUiBrowser\(browser\)\)/);
const pathGuard = from(bridge, 'const auto can_reveal_download =',
                       'if (m == "premium.status")');
assert.match(pathGuard, /c < 0x20/);
assert.match(pathGuard, /c == 0x7f/);
assert.match(pathGuard, /c == '\"'/);
assert.ok(pathGuard.includes(`c == '${String.fromCharCode(92)}''`));
assert.match(pathGuard, /candidate\.is_absolute\(\)/);
assert.match(pathGuard, /component == "\.\."/);
assert.match(pathGuard, /security::IsPathWithin\(downloads, normalized\)/);
assert.match(pathGuard, /accepted_download_paths_\.count\(raw_path\)/);
assert.match(pathGuard, /security::IsRegularFileWithoutLinkChain\(normalized\)/);
const show = from(bridge, 'if (m == "shell.show")', 'if (m == "block.setEnabled")');
assert.match(show, /if \(can_reveal_download\(path\)\) platform::ShowInFolder\(path\)/);
const fileState = from(bridge, 'if (m == "shell.filestate")',
                       'if (m == "dl.decision"');
assert.match(fileState, /can_reveal_download\(Str\(a, "path"\)\)/);

// Только подтверждённая загрузка может пополнить ограниченный список путей.
const decision = from(shell, 'bool Shell::DownloadDecision(',
                       'bool Shell::DownloadControl(');
assert.match(decision, /accepted_downloads_\[id\] = pd\.filename/);
const update = from(shell, 'void Shell::OnTabDownloadUpdated(', '\n}\n\n}  // namespace shelter');
assert.match(update, /auto accepted = accepted_downloads_\.find\(id\)/);
assert.match(update, /if \(accepted == accepted_downloads_\.end\(\)\) return/);
assert.match(update, /accepted_download_paths_\.insert\(path\)/);
assert.match(update, /kMaxAcceptedDownloadPaths/);
assert.match(shellHeader, /accepted_download_paths_/);
assert.match(shellHeader, /accepted_download_path_order_/);
assert.match(shell, /accepted_download_paths_\.clear\(\)/);
assert.match(shell, /accepted_download_path_order_\.clear\(\)/);

// Проверка всей цепочки исключает symlink/reparse point в файле и его родителях.
const chainGuard = from(paths, 'inline bool IsRegularFileWithoutLinkChain(',
                        'inline bool EnsureDirectoryWithoutLink(');
assert.match(chainGuard, /path\.is_absolute\(\)/);
assert.match(chainGuard, /component == "\.\."/);
assert.match(chainGuard, /IsLinkOrReparsePoint\(current\)/);
assert.match(chainGuard, /IsDirectoryWithoutLink\(current\)/);
assert.match(chainGuard, /IsRegularFileWithoutLink\(current\)/);
assert.match(pathTest, /regular file without linked ancestors accepted/);
assert.match(pathTest, /symlink in file path rejected/);
assert.match(pathTest, /file symlink rejected/);

// Windows использует Shell PIDL API, а не строку аргументов explorer.exe.
const winShow = from(win, 'void ShowInFolder(', 'void OpenExternal(');
assert.match(winShow, /SHParseDisplayName\(/);
assert.match(winShow, /SHOpenFolderAndSelectItems\(/);
assert.match(winShow, /ILRemoveLastID\(/);
assert.doesNotMatch(winShow, /ShellExecuteW|explorer\.exe/);

// macOS передаёт только file URL через NSWorkspace.
const macShow = from(mac, 'void ShowInFolder(', 'void OpenExternal(');
assert.match(macShow, /fileURLWithPath/);
assert.match(macShow, /isFileURL/);
assert.match(macShow, /activateFileViewerSelectingURLs/);
assert.doesNotMatch(macShow, /system\(|popen\(|NSTask/);

// UI передаёт только сохранённый путь записи загрузки через прежний метод.
assert.match(hostBridge, /showInFolder: function \(p\) \{ return q\('shell\.show', \{ path: String\(p \|\| ''\) \}\); \}/);
const reveal = from(ui, 'function dlReveal(d) {', '\nlet noteT =');
assert.match(reveal, /showInFolder\(d\.path\)/);

console.log('Show-in-folder path security source checks passed');
