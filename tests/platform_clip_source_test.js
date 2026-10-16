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

const mac = read('src/platform_mac.mm');
const applyClip = mac.slice(
  mac.indexOf('void ApplyViewClip(void* handle'),
  mac.indexOf('static void DumpView', mac.indexOf('void ApplyViewClip(void* handle')));
assert.match(applyClip, /g_view_clip_cache\.radii == current_radii/);
assert.match(applyClip, /g_view_clip_cache\.holes == holes/);
assert.match(applyClip, /g_view_clip_cache\.view_w == view_w/);
assert.match(applyClip, /g_view_clip_cache\.content_w == width/);
assert.match(applyClip, /g_view_clip_cache\.visible == visible/);
assert.match(applyClip, /if \(same_geometry && g_view_clip_cache\.visible == visible[\s\S]*?return;/);
assert.match(applyClip, /\[CATransaction setDisableActions:YES\]/);
assert.match(applyClip, /CGPathEqualToPath\(mask\.path, flipped\)/);
assert.match(applyClip, /if \(!mask\) \{[\s\S]*?\[CAShapeLayer layer\]/);
assert.doesNotMatch(applyClip, /layer\.mask\s*=\s*nil/,
                    'hide/show must preserve the attached shape mask');

console.log('Platform clip-mask source regression test passed');
