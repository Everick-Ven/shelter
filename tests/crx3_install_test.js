'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const shell = read('src/shell.cc');
const shellHeader = read('src/shell.h');
const format = read('src/crx3_format.h');
const verifier = read('src/crx3.cc');
const platformHeader = read('src/platform.h');
const windows = read('src/platform_win.cc');
const mac = read('src/platform_mac.mm');
const ui = read('resources/ui/index.html');
const cmake = read('CMakeLists.txt');

const from = (text, start, end) => {
  const i = text.indexOf(start);
  assert.notEqual(i, -1, `missing source section: ${start}`);
  const j = text.indexOf(end, i + start.length);
  assert.notEqual(j, -1, `missing source boundary: ${end}`);
  return text.slice(i, j);
};

// Адрес CRX разбирается по компонентам URL, без поиска расширения в строке.
const install = from(shell, 'void Shell::ExtInstall(const std::string& src)',
                     'void Shell::ExtInstallBytes(');
assert.match(install, /CefParseURL\(s, parts\)/);
assert.match(install, /CefString\(&parts\.path\)/);
assert.match(install, /scheme != "https"/);
assert.match(install, /lower_path\.compare\(lower_path\.size\(\) - 4, 4, "\.crx"\)/);
assert.doesNotMatch(install, /s\.find\("\.crx"\)/);
assert.match(install, /IsChromeExtensionId\(id\)/);
assert.match(install, /new CrxDownload\(id\)/);

// Редиректы повторно проверяются до передачи данных установщику.
const download = from(shell, 'class CrxDownload : public CefURLRequestClient',
                       'bool UnzipBytesTo(');
assert.match(download, /request->GetResponse\(\)/);
assert.match(download, /response->GetURL\(\)/);
assert.match(download, /scheme != "https"/);
assert.match(download, /ExtInstallBytes\(std::move\(buf_\), expected_id_, false\)/);
assert.doesNotMatch(shell, /CrxZipOffset/);

// Все пакеты проходят проверку CRX3; ZIP допустим только как локальный выбор,
// в режиме оболочки, а имя и разрешения передаются в подтверждение UI.
const installBytes = from(shell, 'void Shell::ExtInstallBytes(',
                           'void Shell::ResolveExtensionInstall(');
assert.match(installBytes, /crx3::Verify\(bytes, expected_id/);
assert.match(installBytes, /if \(!local_file\)/);
assert.match(installBytes, /if \(ExtEngineModeOn\(\)\)/);
assert.match(installBytes, /ReadManifestPermissionField\(dictionary, "permissions"/);
assert.match(installBytes, /"host_permissions"/);
assert.match(installBytes, /UiEvent\("ext-install-prompt"/);
assert.match(installBytes, /pending\.archive_bytes = std::move\(bytes\)/);
const confirmInstall = from(shell, 'void Shell::ResolveExtensionInstall(',
                            '// Косметическая фильтрация:');
assert.match(confirmInstall, /crx3::Verify\(pending\.archive_bytes/);
assert.match(confirmInstall, /CreateTemporaryExtensionDirectory\(root, &tmp\)/);
assert.match(shellHeader, /pending_extension_installs_/);
assert.match(shellHeader, /archive_bytes/);
assert.match(shellHeader, /ResolveExtensionInstall\(/);

// Разбор ограничен CRX3 и проверяет proof, signed_header_data и CRX ID.
assert.match(format, /bytes\.compare\(0, 4, "Cr24"\)/);
assert.match(format, /read_le32\(4\) != 3/);
assert.match(format, /kMaxHeaderBytes/);
assert.match(format, /ParseSignedData\(signed_data, &parsed\.crx_id\)/);
assert.match(format, /parsed\.signed_header_data = std::move\(signed_data\)/);
assert.match(verifier, /proof_id_bytes != package\.crx_id/);
assert.match(verifier, /proof_id != expected_id/);
assert.match(verifier, /platform::VerifyCrx3Signature/);
assert.match(platformHeader, /bool VerifyCrx3Signature\(/);
assert.match(windows, /BCryptVerifySignature\(/);
assert.match(windows, /CryptImportPublicKeyInfoEx2\(/);
assert.match(mac, /SecKeyVerifySignature\(/);
assert.match(mac, /CC_SHA256_Update\(/);
assert.match(cmake, /src\/crx3\.cc src\/crx3\.h src\/crx3_format\.h/);

// Подтверждение использует существующий confirmDlg и существующий bridge-метод.
const prompt = from(ui, "case 'ext-install-prompt': {", "case 'ext-toast':");
assert.match(prompt, /confirmDlg\(/);
assert.match(prompt, /list: requested/);
assert.match(prompt, /native\.extInstall\(/);
assert.match(prompt, /shelter-ext-confirm:/);
console.log('CRX3 installation security source checks passed');
