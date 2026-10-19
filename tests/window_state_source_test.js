'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');

const source = fs.readFileSync(path.join(__dirname, '..', 'src', 'shell.cc'), 'utf8');
const stateStart = source.indexOf('constexpr char kMainWindowStateFile[]');
const stateEnd = source.indexOf('\nclass ShellWindowDelegate', stateStart);
assert(stateStart >= 0 && stateEnd > stateStart, 'в исходнике должен быть выделен блок сохранения режима окна');
const state = source.slice(stateStart, stateEnd);

for (const mode of ['normal', 'maximized', 'fullscreen']) {
  assert(state.includes(`return "${mode}"`), `режим окна ${mode} должен сохраняться явно`);
}
assert(state.includes('platform::UserDataDir()'), 'настройка окна должна храниться в каталоге данных приложения');
assert(state.includes('security::EnsureDirectoryWithoutLink(directory)'), 'каталог настроек проверяется без перехода по ссылкам');
assert(state.includes('security::IsRegularFileWithoutLink(path)'), 'режим читается только из обычного файла');
assert(state.includes('MOVEFILE_REPLACE_EXISTING') && state.includes('fs::rename(temporary, path, rename_error)'), 'запись режима заменяется атомарно на Windows и macOS');
assert(state.includes('window->IsMinimized()'), 'сворачивание не должно затирать последний развёрнутый режим');

const delegateStart = source.indexOf('class ShellWindowDelegate');
const delegateEnd = source.indexOf('class PopupWindowDelegate', delegateStart);
assert(delegateStart >= 0 && delegateEnd > delegateStart, 'главное окно должно иметь отдельный delegate');
const delegate = source.slice(delegateStart, delegateEnd);
assert(delegate.includes('GetInitialShowState') && delegate.includes('ReadMainWindowMode()'), 'при запуске читается сохранённый режим окна');
assert(delegate.includes('CEF_SHOW_STATE_NORMAL') && delegate.includes('CEF_SHOW_STATE_MAXIMIZED') && delegate.includes('CEF_SHOW_STATE_FULLSCREEN'), 'CEF получает обычный, развёрнутый или полноэкранный режим');
assert(delegate.includes('OnWindowBoundsChanged') && delegate.includes('SaveMainWindowMode(window)'), 'изменения размера и развёртывания сохраняются');
assert(delegate.includes('OnWindowFullscreenTransition') && delegate.includes('if (is_completed)'), 'полноэкранный режим сохраняется после перехода');
assert(source.includes('SaveMainWindowMode(window_);'), 'последний режим сохраняется также при закрытии главного окна');
assert(delegate.includes('WithStandardWindowButtons') && delegate.includes('#if defined(OS_MAC)'), 'нативные кнопки окна остаются только на macOS, на Windows их рисует вёрстка');

console.log('Проверка исходника восстановления режима окна прошла');
