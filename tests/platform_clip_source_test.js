'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const win = read('src/platform_win.cc');
assert.match(win, /kClipMaskClass[\s\S]*?WS_CHILD/,
             'Windows clip mask must be a child window');
assert.doesNotMatch(win, /WS_POPUP|SetWinEventHook|UnhookWinEvent/,
                    'Windows clip mask must not be top-level or track moves with hooks');
assert.match(win, /GetAncestor\(view, GA_ROOT\)/,
             'Windows clip mask must be parented to the root HWND');
assert.match(win, /case WM_NCHITTEST:\s*return HTTRANSPARENT;/);
assert.doesNotMatch(win, /ClientToScreen/,
                    'child mask position must use root client coordinates');
assert.match(win, /SetWindowPos\(g_clip_mask_window, HWND_TOP, left, top, width, height/);
assert.match(win, /bool same_content = g_clip_drawn_valid/);
assert.match(win, /if \(same_content\) \{[\s\S]*?return;[\s\S]*?HDC screen_dc = GetDC/,
             'unchanged mask content must return before creating a new bitmap');
assert.match(win, /g_clip_drawn_visible/);

console.log('Windows clip-mask source regression test passed');
