'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const shell = read('src/shell.cc');
const header = read('src/shell.h');
const bridge = read('src/shell_bridge.cc');
const win = read('src/platform_win.cc');
const visibility = shell.slice(
  shell.indexOf('void Shell::SetOverlayVisibility'),
  shell.indexOf('void Shell::HideAllTabs'));
const layout = shell.slice(
  shell.indexOf('void Shell::LayoutTab'),
  shell.indexOf('void Shell::OnUiReady'));
const clip = shell.slice(
  shell.indexOf('void Shell::ApplyClip'),
  shell.indexOf('void Shell::Log'));

assert.match(visibility, /!tab->has_last_overlay_visibility/);
assert.match(visibility, /tab->last_overlay_visibility != visible/);
assert.match(visibility, /tab->overlay->SetVisible\(visible\)/);
assert.match(layout, /tab->has_last_bounds \|\| !SameRect\(tab->last_bounds, rect\)/);
assert.match(layout, /tab->overlay->SetBounds\(rect\)/);
assert.match(layout, /tab->pending_rect = rect/);
assert.match(layout, /tab->has_pending_layout = true/);
assert.match(layout, /shell: content bounds shown/);
assert.doesNotMatch(bridge, /overlay->SetVisible/);
assert.match(header, /bool has_last_bounds = false/);
assert.match(header, /bool has_last_overlay_visibility = false/);

assert.match(clip, /tab->last_clip_radii == radii/);
assert.match(clip, /tab->last_clip_holes == clip_holes_/);
assert.match(clip, /tab->last_clip_mask_color == clip_mask_color_/);
assert.match(clip, /tab->last_clip_visible == visible/);
assert.match(clip, /if \(tab->has_last_clip[\s\S]*?return;[\s\S]*?platform::ApplyViewClip/);
assert.match(header, /bool has_last_clip = false/);
const tabCreated = shell.slice(
  shell.indexOf('void Shell::OnTabCreated'),
  shell.indexOf('void Shell::DestroyTab'));
assert.match(tabCreated, /t->browser = browser;[\s\S]*?if \(t->view_visible\) ApplyClip\(t\)/,
             'clip is applied when an asynchronously created browser becomes ready');
assert.match(bridge, /t->has_last_clip = false;[\s\S]*?ApplyClip\(t\)/,
             'debug clip reset must invalidate the common clip cache before reapplying');

const switchTab = bridge.slice(
  bridge.indexOf('if (m == "view.open")'),
  bridge.indexOf('if (m == "view.layout")'));
assert.match(switchTab, /active_tab_ = id;[\s\S]*?LayoutTab\(t, RectOf\(a\), view_visible\)/);
assert.doesNotMatch(switchTab, /SetVisible\(false\)/);
assert.match(layout, /ApplyClip\(tab\);[\s\S]*?if \(tab->id == active_tab_\) HideInactiveTabs\(tab\)/,
             'the new active overlay must be shown and clipped before other tabs are hidden');
assert.match(layout, /tab->has_pending_layout = true;[\s\S]*?return;/,
             'a pending browser keeps the existing tab visible until its view can be shown');
assert.match(win, /if \(!visible\) \{\s*if \(!view \|\| view == g_clip_mask_view\) HideClipMask\(\)/,
             'hiding the old Windows tab must not hide the new tab clip mask');

console.log('Shell overlay and clip deduplication regression test passed');
