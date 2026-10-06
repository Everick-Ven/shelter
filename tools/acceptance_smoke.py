#!/usr/bin/env python3
"""Strict packaged-app acceptance checks for the SHELTER UI and native tabs.

The test drives the real packaged executable through the same CEF DevTools
endpoint used by tools/smoke.py. It fails unless the dashboard fits at several
viewport sizes and a real HTTPS page loads in a native browser tab.
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    import websocket
except ImportError as exc:  # pragma: no cover - exercised by CI setup
    raise SystemExit("Missing dependency: install websocket-client") from exc


class AcceptanceError(RuntimeError):
    pass


class LongPageHandler(BaseHTTPRequestHandler):
    """Deterministic long native pages for scroll/overlay regression coverage."""

    protocol_version = "HTTP/1.1"

    def log_message(self, _format: str, *_args: Any) -> None:
        pass

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        path = self.path.split("?", 1)[0]
        if path == "/favicon.ico":
            self.send_response(204)
            self.end_headers()
            return
        variant = path.rsplit("/", 1)[-1]
        palettes = {
            "a": ("#ffffff", "#152033", "#1264d8"),
            "b": ("#111827", "#f3f4f6", "#2dd4bf"),
            "c": ("#f7e5d5", "#38202b", "#a33251"),
            "loading": ("#f4f7fb", "#14233a", "#4b54dc"),
        }
        if variant not in palettes:
            self.send_error(404)
            return
        background, foreground, accent = palettes[variant]
        sections = "".join(
            "<section id='section-{0}'><small>{1} · SECTION {0:03d}</small>"
            "<h2>Scrollable native document {0}</h2><p>"
            "This deterministic long page checks compositor isolation while "
            "the browser UI opens and closes its menus. The content remains "
            "available throughout rapid scrolling and navigation.</p></section>".format(
                index, variant.upper()
            )
            for index in range(1, 91)
        )
        document = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SHELTER long-page {variant}</title>
<style>
*{{box-sizing:border-box}}html,body{{margin:0;scroll-behavior:auto}}
body{{background:{background};color:{foreground};font:16px/1.6 system-ui,sans-serif}}
header{{position:sticky;top:0;z-index:1;padding:16px 24px;background:{accent};color:#fff;font-weight:700}}
main{{max-width:980px;margin:0 auto;padding:0 20px}}
section{{min-height:520px;padding:54px 28px;border-bottom:1px solid currentColor;opacity:.98}}
section small{{color:{accent};font-weight:800;letter-spacing:.14em}}
h2{{font-size:clamp(24px,4vw,42px);line-height:1.15}}
p{{max-width:60ch}}
</style></head><body><header>Long native page · {variant.upper()}</header>
<main>{sections}</main></body></html>""".encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(document)))
        self.end_headers()
        try:
            if variant == "loading":
                split = document.find(b"<main>")
                if split < 0:
                    split = len(document) // 4
                self.wfile.write(document[:split])
                self.wfile.flush()
                time.sleep(2.0)
                self.wfile.write(document[split:])
            else:
                self.wfile.write(document)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass


def start_long_page_server() -> Tuple[ThreadingHTTPServer, threading.Thread, str]:
    server = ThreadingHTTPServer(("127.0.0.1", 0), LongPageHandler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, name="acceptance-long-pages", daemon=True)
    thread.start()
    return server, thread, f"http://127.0.0.1:{server.server_address[1]}"


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def get_targets(port: int) -> List[Dict[str, Any]]:
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/json/list",
        headers={"Cache-Control": "no-cache"},
    )
    with urllib.request.urlopen(request, timeout=3) as response:
        return json.load(response)


class Cdp:
    def __init__(self, websocket_url: str):
        self.socket = websocket.create_connection(
            websocket_url, timeout=12, suppress_origin=True
        )
        self.next_id = 0
        self.events: List[Dict[str, Any]] = []

    def call(
        self,
        method: str,
        params: Optional[Dict[str, Any]] = None,
        timeout: float = 12,
    ) -> Dict[str, Any]:
        self.next_id += 1
        call_id = self.next_id
        self.socket.send(
            json.dumps({"id": call_id, "method": method, "params": params or {}})
        )
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                message = json.loads(self.socket.recv())
            except Exception as exc:
                raise AcceptanceError(f"CDP {method} receive failed: {exc}") from exc
            if message.get("id") == call_id:
                if "error" in message:
                    raise AcceptanceError(
                        f"CDP {method} failed: {message['error']}"
                    )
                return message.get("result", {})
            if message.get("method"):
                self.events.append(message)
        raise AcceptanceError(f"CDP {method} timed out after {timeout:g}s")

    def evaluate(self, expression: str, timeout: float = 12) -> Any:
        response = self.call(
            "Runtime.evaluate",
            {
                "expression": expression,
                "returnByValue": True,
                "awaitPromise": True,
            },
            timeout=timeout,
        )
        if "exceptionDetails" in response:
            details = response["exceptionDetails"]
            raise AcceptanceError(
                "JavaScript evaluation failed: "
                + json.dumps(details, ensure_ascii=False)[:800]
            )
        return response.get("result", {}).get("value")

    def close(self) -> None:
        try:
            self.socket.close()
        except Exception:
            pass


def wait_until(
    probe,
    description: str,
    process: subprocess.Popen,
    timeout: float,
    interval: float = 0.4,
):
    deadline = time.monotonic() + timeout
    last_error: Optional[Exception] = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AcceptanceError(
                f"SHELTER exited early with status {process.returncode} "
                f"while waiting for {description}"
            )
        try:
            value = probe()
            if value:
                return value
        except Exception as exc:
            last_error = exc
        time.sleep(interval)
    detail = f"; last error: {last_error}" if last_error else ""
    raise AcceptanceError(f"Timed out waiting for {description}{detail}")


def viewport_measurement(ui: Cdp) -> Dict[str, Any]:
    expression = r"""
      (function() {
        const viewport = document.getElementById('viewport');
        const page = document.getElementById('page');
        const top = document.querySelector('.dash-top');
        const right = document.querySelector('.dash-right');
        const bottom = document.querySelector('.dash-bottom');
        if (!viewport || !page || !top) return null;
        const edge = viewport.getBoundingClientRect().right;
        const pageEdge = page.getBoundingClientRect().right;
        let overflow = 0;
        document.querySelectorAll(
          '.dash-top > *, .dash-right > *, .dash-bottom > *'
        ).forEach(function(card) {
          const rect = card.getBoundingClientRect();
          if (rect.width > 0 && rect.height > 0)
            overflow = Math.max(overflow, rect.right - edge);
        });
        const dashboardNodes = [page, top, right, bottom].concat(
          Array.from(page.querySelectorAll(
            '.page-h, .page-actions, .card, .card-h, .stats-body, .kpis, .donut-wrap, .meter'
          ))
        );
        dashboardNodes.forEach(function(el) {
          if (el) overflow = Math.max(overflow, el.scrollWidth - el.clientWidth);
        });
        const overflowNodes = Array.from(page.querySelectorAll('*')).map(function(el) {
          const rect = el.getBoundingClientRect();
          const rightPx = Math.max(0, rect.right - pageEdge);
          const scrollPx = Math.max(0, el.scrollWidth - el.clientWidth);
          const amount = Math.max(rightPx, scrollPx);
          if (amount <= 1 || rect.width <= 0 || rect.height <= 0) return null;
          const classes = Array.from(el.classList || []).slice(0, 2).join('.');
          return {
            element: el.tagName.toLowerCase() + (el.id ? '#' + el.id : '') +
              (classes ? '.' + classes : ''),
            rightPx: Math.round(rightPx),
            scrollPx: Math.round(scrollPx),
            width: Math.round(rect.width),
            scrollWidth: el.scrollWidth
          };
        }).filter(Boolean).sort(function(a, b) {
          return Math.max(b.rightPx, b.scrollPx) - Math.max(a.rightPx, a.scrollPx);
        }).slice(0, 8);
        return JSON.stringify({
          windowWidth: window.innerWidth,
          viewportWidth: viewport.clientWidth,
          pageWidth: page.clientWidth,
          pageScrollWidth: page.scrollWidth,
          documentOverflowPx: Math.max(0, document.documentElement.scrollWidth - document.documentElement.clientWidth),
          overflowPx: Math.max(0, overflow),
          overflowNodes: overflowNodes
        });
      })()
    """
    raw = ui.evaluate(expression)
    if not raw:
        return {}
    return json.loads(raw)


def sidebar_scroll_probe(ui: Cdp, width: int, height: int) -> Dict[str, Any]:
    """Exercise intrinsic growth, overflow, and collapse of the real sidebar."""
    ui.call(
        "Emulation.setDeviceMetricsOverride",
        {
            "width": width,
            "height": height,
            "deviceScaleFactor": 1,
            "mobile": False,
            "screenWidth": width,
            "screenHeight": height,
        },
    )
    expression = r"""
      (async function() {
        const scroll = document.getElementById('sbScroll');
        const tabs = document.getElementById('tabList');
        const ext = document.querySelector('#sbInner .ni[data-page="extensions"]');
        if (!scroll || !tabs || !ext || tabs.parentElement !== document.getElementById('dockLeft'))
          return null;
        const originalMarkup = tabs.innerHTML;
        const originalScrollTop = scroll.scrollTop;
        const nextLayout = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const measure = () => {
          const scrollRect = scroll.getBoundingClientRect();
          const extRect = ext.getBoundingClientRect();
          return {
            clientHeight: scroll.clientHeight,
            scrollHeight: scroll.scrollHeight,
            scrollTop: scroll.scrollTop,
            maxScroll: Math.max(0, scroll.scrollHeight - scroll.clientHeight),
            scrollBottom: Math.round(scrollRect.bottom * 100) / 100,
            extensionsBottom: Math.round(extRect.bottom * 100) / 100,
            gapAfterExtensionsPx: Math.round((scrollRect.bottom - extRect.bottom) * 100) / 100
          };
        };
        const addProbeTabs = count => {
          tabs.replaceChildren();
          for (let i = 0; i < count; i++) {
            const row = document.createElement('div');
            row.className = 'tab';
            row.setAttribute('data-acceptance-probe', '1');
            row.innerHTML = '<span class="fav int">S</span>' +
              '<span class="t">Acceptance tab ' + (i + 1) + '</span>';
            tabs.appendChild(row);
          }
        };
        try {
          addProbeTabs(0);
          scroll.scrollTop = 0;
          await nextLayout();
          const empty = measure();

          addProbeTabs(1);
          await nextLayout();
          const oneTab = measure();

          addProbeTabs(30);
          await nextLayout();
          const manyTabs = measure();
          scroll.scrollTop = scroll.scrollHeight;
          await nextLayout();
          const atBottom = measure();

          addProbeTabs(0);
          scroll.scrollTop = 0;
          await nextLayout();
          const collapsed = measure();
          return JSON.stringify({
            viewport: { width: innerWidth, height: innerHeight },
            empty: empty,
            oneTab: oneTab,
            manyTabs: manyTabs,
            atBottom: atBottom,
            collapsed: collapsed
          });
        } finally {
          tabs.innerHTML = originalMarkup;
          scroll.scrollTop = originalScrollTop;
        }
      })()
    """
    raw = ui.evaluate(expression, timeout=20)
    if not raw:
        raise AcceptanceError(
            f"Sidebar scroll probe could not find the left-tab layout at {width}x{height}"
        )
    return json.loads(raw)


def toolbar_menu_measurement(ui: Cdp) -> Dict[str, Any]:
    """Verify that toolbar actions and the trimmed overflow menu stay in place."""
    expression = r"""
      (async function() {
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const quick = document.getElementById('quickBtn');
        const settings = document.getElementById('settingsBtn');
        const more = document.getElementById('moreBtn');
        const controls = document.getElementById('ctlGrid');
        const mode = document.getElementById('schemeModeBtn');
        const header = document.querySelector('.tb');
        const fire = controls && controls.querySelector('.ctl.fire');
        if (!quick || !settings || !more || !controls || !mode || !fire || !header)
          return null;
        const fireRect = fire.getBoundingClientRect();
        const modeRect = mode.getBoundingClientRect();
        const quickRect = quick.getBoundingClientRect();
        const settingsRect = settings.getBoundingClientRect();
        const moreRect = more.getBoundingClientRect();
        const headerRect = header.getBoundingClientRect();
        const toolbar = {
          settingsBeforeMenu: settings.parentElement === more.parentElement &&
            settings.nextElementSibling === more &&
            Math.abs(settingsRect.top - moreRect.top) <= 1 &&
            Math.abs(moreRect.left - settingsRect.right) <= 12,
          themeAfterFire: controls.lastElementChild === fire &&
            mode.previousElementSibling === controls &&
            Math.abs(modeRect.top - fireRect.top) <= 1 &&
            modeRect.left >= fireRect.right - 1,
          extraGapBeforeQuick: quickRect.left - modeRect.right >= 20,
          quickLabelUpdated: quick.dataset.tip === 'Оформление и вкладки' &&
            quick.getAttribute('aria-label') === 'Оформление и вкладки',
          noHorizontalOverflow: header.scrollWidth <= header.clientWidth + 1 &&
            headerRect.right <= innerWidth + 1,
          sidebarClean: !document.querySelector('#sb [data-act="schemeMode"], #sb #swatches, #sb [data-act="settings"], #sb [data-act="account"]')
        };
        more.click();
        await frame();
        const menu = document.querySelector('.menu:not(.closing)');
        if (!menu) return null;
        const dashboardFallbackDisabled = !document.body.classList.contains('native-content-visible') &&
          !getComputedStyle(menu).getPropertyValue('--native-popup-opacity').trim();
        const labels = Array.from(menu.querySelectorAll('.mi .lb')).map(el => el.innerText.trim());
        const forbidden = ['Новая вкладка', 'Новое пространство', 'Режим «Призрак»',
          'Выключить «Призрак»', 'История', 'Загрузки', 'Пароли', 'Сертификаты РФ', 'Настройки'];
        const visibleForbidden = forbidden.filter(label => menu.innerText.includes(label));
        const menuItems = Array.from(menu.querySelectorAll('.mi'));
        const lastItem = menuItems[menuItems.length - 1];
        const profileLast = !!lastItem &&
          lastItem.querySelector('.lb')?.innerText.trim() === 'Профиль' &&
          !!lastItem.querySelector('.avatar');
        quick.click();
        await frame();
        const quickMenu = document.querySelector('.menu-quick');
        const opacity = quickMenu && quickMenu.querySelector('#glassOpSec');
        const palette = quickMenu && quickMenu.querySelector('#quickThemeSec');
        const swatchWrap = palette && palette.querySelector('.q-theme-swatches');
        const swatches = swatchWrap ? Array.from(swatchWrap.querySelectorAll('.swatch[data-theme]')) : [];
        const wrapRect = swatchWrap && swatchWrap.getBoundingClientRect();
        const firstRect = swatches[0] && swatches[0].getBoundingClientRect();
        const lastRect = swatches[swatches.length - 1] && swatches[swatches.length - 1].getBoundingClientRect();
        const leftInset = wrapRect && firstRect ? firstRect.left - wrapRect.left : null;
        const rightInset = wrapRect && lastRect ? wrapRect.right - lastRect.right : null;
        const paletteState = {
          immediatelyAfterOpacity: !!opacity && opacity.nextElementSibling === palette,
          swatchCount: swatches.length,
          leftInset: leftInset,
          rightInset: rightInset,
          equalSideInsets: leftInset !== null && rightInset !== null && Math.abs(leftInset - rightInset) <= 1.5
        };
        quick.click();
        await frame();
        return JSON.stringify({
          viewportWidth: innerWidth,
          dashboardFallbackDisabled: dashboardFallbackDisabled,
          toolbar: toolbar,
          menuLabels: labels,
          visibleForbidden: visibleForbidden,
          profileLast: profileLast,
          palette: paletteState
        });
      })()
    """
    raw = ui.evaluate(expression, timeout=20)
    if not raw:
        raise AcceptanceError("Toolbar/menu layout probe could not inspect the UI")
    return json.loads(raw)


def performance_mode_probe(ui: Cdp) -> Dict[str, Any]:
    """Exercise all six performance-mode transitions, rapid repeats, and both controls."""
    expression = r"""
      (async function() {
        const test = window.shelterTest;
        if (!test || !test.setGfxMode || !test.gfxSnapshot)
          throw new Error('performance test surface is missing');
        const original = test.state().prefs.gfx || 'balance';
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const verify = (expected, snap) => {
          const issues = [];
          const visual = expected === 'beauty';
          const speed = expected === 'perf';
          if (snap.gfx !== expected || snap.dataGfx !== expected) issues.push('mode state mismatch');
          if (snap.motion !== (speed ? 'off' : 'full')) issues.push('animation state mismatch');
          if (snap.glow !== visual || snap.glowOff === visual) issues.push('glow state mismatch');
          if (snap.reduceMotion !== speed) issues.push('reduced-motion class mismatch');
          if (visual ? !snap.blur.includes('blur(') : snap.blur !== 'none') issues.push('glass blur mismatch');
          if (visual ? !snap.menuBlur.includes('blur(') : snap.menuBlur !== 'none') issues.push('menu blur mismatch');
          if (visual ? !snap.glassBackdrop.includes('blur(') : snap.glassBackdrop !== 'none') issues.push('surface backdrop-filter mismatch');
          if (!visual && snap.glowSoft !== 'none') issues.push('glow shadow token survived mode change');
          if (visual ? snap.animation === 'none' : snap.animation !== 'none') issues.push('ambient animation mismatch');
          const hasTransition = snap.transition.split(',').some(x => parseFloat(x) > 0);
          if (speed ? hasTransition : !hasTransition) issues.push('ordinary UI transition mismatch');
          if (speed && snap.shadow !== 'none') issues.push('Speed shadow was not removed');
          if (speed && snap.backgroundImage !== 'none') issues.push('Speed gradient/glow background remains');
          const alpha = snap.cardBackground.match(/rgba\([^,]+,[^,]+,[^,]+,\s*([0-9.]+)\)/i);
          if (speed && alpha && parseFloat(alpha[1]) < 0.999) issues.push('Speed surface is translucent');
          if (!speed && Math.round(parseFloat(snap.glassOpacity)) !== Math.round(+test.state().prefs.glassOp || 82)) issues.push('glass opacity preference changed');
          if (speed && Math.round(parseFloat(snap.glassOpacity)) !== 100) issues.push('Speed is not opaque');
          if (snap.hero && snap.hero.balanceCache) issues.push('stale cached Hero frame');
          if (!visual && snap.hero && snap.hero.canvasVisibility !== 'hidden') issues.push('Hero canvas is not cleared');
          if (!visual && snap.hero && snap.hero.offscreenBuffers !== 0) issues.push('Hero glow cache survived mode change');
          if (!visual && snap.spotLights) issues.push('cursor glow class survived mode change');
          if (document.documentElement.classList.contains('theme-anim') || document.documentElement.classList.contains('vt-theme')) issues.push('theme transition class leaked');
          const amb = document.querySelector('.ambient'), fx = document.getElementById('heroFx');
          if ((amb && (amb.style.opacity || amb.style.transition)) || (fx && (fx.style.opacity || fx.style.transition))) issues.push('inline glow fade leaked');
          return issues;
        };
        const checked = [];
        const plan = ['beauty', 'balance', 'beauty', 'balance', 'perf', 'balance',
          'beauty', 'perf', 'beauty', 'perf', 'balance', 'perf', 'beauty'];
        for (const mode of plan) {
          test.setGfxMode(mode, {persist:false, notify:false});
          await frame();
          const snap = test.gfxSnapshot();
          const issues = verify(mode, snap);
          checked.push({mode:mode, issues:issues, snapshot:snap});
          if (issues.length) throw new Error('mode ' + mode + ': ' + issues.join(', '));
        }
        // Several complete cycles without yielding to rAF catch delayed callbacks
        // or inline styles that could survive rapid toggles.
        const rapid = ['beauty', 'balance', 'perf', 'beauty', 'perf', 'balance'];
        for (let cycle = 0; cycle < 5; cycle++)
          rapid.forEach(mode => test.setGfxMode(mode, {persist:false, notify:false}));
        await frame();
        let rapidSnap = test.gfxSnapshot();
        let rapidIssues = verify('balance', rapidSnap);
        if (rapidIssues.length) throw new Error('rapid switching: ' + rapidIssues.join(', '));

        // Exercise the quick-menu segmented control itself, not just its setter.
        const quick = document.getElementById('quickBtn');
        const qTarget = original === 'perf' ? 'beauty' : 'perf';
        quick.click(); await frame();
        const quickMenu = document.querySelector('.menu-quick:not(.closing)');
        const qButton = quickMenu && quickMenu.querySelector('.seg[data-seg="motion"] button[data-v="' + qTarget + '"]');
        if (!qButton) throw new Error('quick-menu mode buttons are missing');
        qButton.click(); await frame();
        const quickModeOpen = !!document.querySelector('.menu-quick:not(.closing)') && test.state().prefs.gfx === qTarget;
        if (!quickModeOpen) throw new Error('quick-menu mode selection closed the menu or failed');
        quick.click(); await frame();

        // Exercise the same segmented control in Settings, then verify the real
        // native-delete API is present without invoking the destructive action.
        test.openSettings('look'); await frame();
        const settings = document.querySelector('.settings');
        const settingsButton = settings && settings.querySelector('.seg[data-seg="motion"] button[data-v="balance"]');
        if (!settingsButton) throw new Error('settings mode buttons are missing');
        if (!settingsButton.classList.contains('on')) settingsButton.click();
        await frame();
        const settingsMode = test.state().prefs.gfx;
        const dataNav = settings && settings.querySelector('.set-nav [data-sec="data"]');
        if (!dataNav) throw new Error('settings data section is missing');
        dataNav.click();
        await new Promise(resolve => setTimeout(resolve, 230));
        await frame();
        const dataSection = settings.querySelector('#set-data');
        const nativeDeleteAvailable = !!(window.shelterNative && window.shelterNative.isNative &&
          typeof window.shelterNative.deleteUserData === 'function' &&
          dataSection && dataSection.querySelector('[data-set="delete-user-data"]'));
        const close = settings && settings.querySelector('[data-set="close"]');
        if (close) close.click();
        await new Promise(resolve => setTimeout(resolve, 230));
        await frame();
        if (!nativeDeleteAvailable) throw new Error('native profile-deletion control is missing');
        if (settingsMode !== 'balance') throw new Error('settings mode selection failed');

        test.setGfxMode(original, {persist:true, notify:false});
        await frame();
        return JSON.stringify({checked:checked.length, rapidCycles:5, quickModeOpen:quickModeOpen,
          settingsMode:settingsMode, nativeDeleteAvailable:nativeDeleteAvailable,
          final:test.gfxSnapshot()});
      })()
    """
    raw = ui.evaluate(expression, timeout=30)
    if not raw:
        raise AcceptanceError("Performance-mode regression probe returned no result")
    result = json.loads(raw)
    if result.get("checked") != 13 or result.get("rapidCycles") != 5:
        raise AcceptanceError(f"Performance-mode probe did not complete: {result}")
    return result


def quick_theme_switch_probe(ui: Cdp) -> Dict[str, Any]:
    """Verify quick-menu theme changes keep the menu open and can be restored."""
    expression = r"""
      (async function() {
        const quick = document.getElementById('quickBtn');
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const waitForTheme = async theme => {
          const deadline = performance.now() + 2500;
          while (performance.now() < deadline) {
            if (document.documentElement.getAttribute('data-theme') === theme) return true;
            await new Promise(resolve => setTimeout(resolve, 25));
          }
          return false;
        };
        const original = document.documentElement.getAttribute('data-theme');
        quick.click(); await frame();
        let menu = document.querySelector('.menu-quick:not(.closing)');
        if (!menu) throw new Error('quick menu did not open');
        const swatches = Array.from(menu.querySelectorAll('.swatch[data-theme]'));
        const target = swatches.find(button => button.dataset.theme !== original);
        if (!target) throw new Error('alternate theme is missing');
        const selected = target.dataset.theme;
        target.click();
        const applied = await waitForTheme(selected); await frame();
        menu = document.querySelector('.menu-quick:not(.closing)');
        const stayedOpen = !!menu;
        const selectedState = menu && menu.querySelector('.swatch[data-theme="' + selected + '"]')?.getAttribute('aria-pressed') === 'true';
        if (!applied || !stayedOpen || !selectedState)
          throw new Error('theme change did not preserve/update quick menu');
        menu.querySelector('.swatch[data-theme="' + original + '"]').click();
        const restored = await waitForTheme(original); await frame();
        const menuAfterRestore = !!document.querySelector('.menu-quick:not(.closing)');
        quick.click(); await frame();
        if (!restored || !menuAfterRestore) throw new Error('original theme could not be restored in open menu');
        return JSON.stringify({original:original, selected:selected, stayedOpen:stayedOpen,
          selectedState:selectedState, restored:restored, menuAfterRestore:menuAfterRestore});
      })()
    """
    raw = ui.evaluate(expression, timeout=20)
    if not raw:
        raise AcceptanceError("Quick-menu theme probe returned no result")
    return json.loads(raw)

def bridge_state(ui: Cdp) -> Dict[str, Any]:
    raw = ui.evaluate(
        "JSON.stringify((function(){const b=window.__shBridgeState?window.__shBridgeState():{};"
        "const p=window.__shPerf||{};return Object.assign({},b,{clipFrames:p.clipFrames||0,"
        "layoutRequests:p.layoutRequests||0,overlayChecks:p.overlayChecks||0,"
        "snap:!!document.getElementById('shSnap'),"
        "snapWait:document.documentElement.classList.contains('sh-snap-wait'),"
        "overlayNodes:document.querySelectorAll('.menu,.scrim,.tip,.call,.toast,.findbar:not([hidden]),.dl-float:not([hidden]),.suggest:not([hidden])').length});})())"
    )
    return json.loads(raw) if raw else {}


def wait_native_overlay_idle(ui: Cdp, process: subprocess.Popen, timeout: float = 12) -> Dict[str, Any]:
    def idle():
        state = bridge_state(ui)
        if not state:
            return False
        return (
            state.get("visible") is True
            and state.get("nativeContentVisible") is True
            and state.get("busy") is False
            and state.get("frozen") is False
            and state.get("clipActive") is False
            and state.get("trackedPopups") == 0
            and state.get("overlayNodes") == 0
            and state.get("snap") is False
            and state.get("snapWait") is False
        )

    wait_until(idle, "native overlay teardown", process, timeout, interval=0.15)
    return bridge_state(ui)


def popup_fallback_probe(ui: Cdp, process: subprocess.Popen) -> Dict[str, Any]:
    """Check actual popup paint styles through Visual → Balance → Speed → Visual."""
    ui.evaluate(
        "(function(){const b=document.getElementById('moreBtn');if(!b)throw new Error('menu button missing');b.click();return true;})()"
    )
    wait_until(
        lambda: ui.evaluate("!!document.querySelector('.menu:not(.closing)')") is True,
        "overflow menu over a native website",
        process,
        timeout=10,
    )
    wait_until(
        lambda: (lambda s: bool(s and s.get("visible") and s.get("frozen") and not s.get("busy")))(bridge_state(ui)),
        "native page freeze for popup",
        process,
        timeout=15,
    )
    expression = r"""
      (async function() {
        const test = window.shelterTest;
        const original = test.state().prefs.gfx || 'balance';
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const alphaOf = value => {
          const color = String(value || '').trim();
          const slash = color.match(/\/\s*([0-9.]+)(%)?\s*\)$/);
          if (slash) return slash[2] ? Number(slash[1]) / 100 : Number(slash[1]);
          if (/^rgba\(/i.test(color)) {
            const parts = color.slice(color.indexOf('(') + 1, -1).split(',');
            return parts.length > 3 ? Number(parts[3]) : null;
          }
          if (/^rgb\(/i.test(color)) return 1;
          return null;
        };
        const checked = [];
        for (const mode of ['beauty', 'balance', 'perf', 'beauty']) {
          test.setGfxMode(mode, {persist:false, notify:false});
          await frame();
          const menu = document.querySelector('.menu:not(.closing)');
          if (!menu) throw new Error('popup disappeared while switching graphics mode');
          const css = getComputedStyle(menu);
          const ambient = document.querySelector('.ambient i');
          checked.push({
            mode:mode,
            nativeContentVisible:document.body.classList.contains('native-content-visible'),
            backgroundColor:css.backgroundColor,
            backgroundAlpha:alphaOf(css.backgroundColor),
            backdropFilter:css.backdropFilter || css.webkitBackdropFilter || 'none',
            animationName:css.animationName,
            animationDuration:css.animationDuration,
            boxShadow:css.boxShadow,
            glowToken:getComputedStyle(document.body).getPropertyValue('--glow-soft').trim(),
            glowOff:document.body.classList.contains('glow-off'),
            reduceMotion:document.documentElement.classList.contains('reduce-motion'),
            ambientPlayState:ambient ? getComputedStyle(ambient).animationPlayState : 'missing',
            fallbackOpacity:getComputedStyle(document.body).getPropertyValue('--native-popup-opacity').trim()
          });
        }
        test.setGfxMode(original, {persist:false, notify:false});
        await frame();
        return JSON.stringify({checked:checked, original:original});
      })()
    """
    raw = ui.evaluate(expression, timeout=20)
    if not raw:
        raise AcceptanceError("Native popup fallback probe returned no result")
    result = json.loads(raw)
    checked = result.get("checked", [])
    if len(checked) != 4 or [item.get("mode") for item in checked] != ["beauty", "balance", "perf", "beauty"]:
        raise AcceptanceError(f"Native popup probe did not complete the mode sequence: {result}")
    for item in checked:
        mode = item["mode"]
        if not item.get("nativeContentVisible"):
            raise AcceptanceError(f"Native-content fallback class missing in {mode}: {item}")
        alpha = item.get("backgroundAlpha")
        if not isinstance(alpha, (int, float)):
            raise AcceptanceError(f"Could not read popup background alpha in {mode}: {item}")
        if mode == "perf":
            if alpha < 0.999:
                raise AcceptanceError(f"Speed popup is not opaque over a native page: {item}")
            if item.get("backdropFilter") != "none" or item.get("animationName") != "none":
                raise AcceptanceError(f"Speed left a popup effect active: {item}")
            if item.get("boxShadow") != "none":
                raise AcceptanceError(f"Speed left a popup glow/shadow: {item}")
            if not item.get("glowOff") or not item.get("reduceMotion"):
                raise AcceptanceError(f"Speed mode flags are incomplete: {item}")
            if item.get("glowToken") != "none":
                raise AcceptanceError(f"Speed retained a glow token: {item}")
        else:
            if alpha < 0.939:
                raise AcceptanceError(f"Popup fallback is too transparent for arbitrary web content: {item}")
            if item.get("animationName") == "none" or item.get("animationDuration") in ("0s", "0.0s"):
                raise AcceptanceError(f"Visual/Balance lost ordinary popup animation: {item}")
            if item.get("glowOff") != (mode != "beauty"):
                raise AcceptanceError(f"Popup glow state does not match {mode}: {item}")
            if (item.get("glowToken") == "none") != (mode != "beauty"):
                raise AcceptanceError(f"Popup glow token does not match {mode}: {item}")
            if item.get("reduceMotion"):
                raise AcceptanceError(f"Reduced-motion state leaked into {mode}: {item}")
            if item.get("ambientPlayState") != "paused":
                raise AcceptanceError(f"Ambient animation is not paused while native content is visible: {item}")
            if mode == "beauty" and "blur(" not in item.get("backdropFilter", ""):
                raise AcceptanceError(f"Visual popup lost backdrop blur: {item}")
            if mode == "balance" and item.get("backdropFilter") != "none":
                raise AcceptanceError(f"Balance popup retained backdrop blur: {item}")
    ui.evaluate("document.getElementById('moreBtn').click()")
    wait_native_overlay_idle(ui, process)
    return result


def start_scroll_sweep(page: Cdp, frames: int = 48) -> Dict[str, Any]:
    expression = r"""
      (function(frames) {
        const scroller = document.scrollingElement || document.documentElement;
        const maxScroll = Math.max(0, scroller.scrollHeight - innerHeight);
        if (maxScroll < 10000) throw new Error('long-page fixture is not scrollable: ' + maxScroll);
        const started = performance.now();
        let frame = 0, minY = scrollY, maxY = scrollY;
        window.__shelterScrollProbe = new Promise(resolve => {
          function tick() {
            const target = Math.round(maxScroll * Math.min(1, (frame + 1) / frames));
            window.scrollTo(0, target);
            minY = Math.min(minY, scrollY); maxY = Math.max(maxY, scrollY);
            frame++;
            if (frame < frames) requestAnimationFrame(tick);
            else requestAnimationFrame(() => {
              window.scrollTo(0, maxScroll);
              resolve({frames:frame, maxScroll:maxScroll, finalY:scrollY,
                minY:minY, maxY:maxY, elapsedMs:Math.round(performance.now()-started)});
            });
          }
          requestAnimationFrame(tick);
        });
        return JSON.stringify({started:true, maxScroll:maxScroll, frames:frames});
      })(""" + str(int(frames)) + ")"
    raw = page.evaluate(expression)
    if not raw:
        raise AcceptanceError("Could not start long-page scroll sweep")
    return json.loads(raw)


def finish_scroll_sweep(page: Cdp) -> Dict[str, Any]:
    raw = page.evaluate("(async()=>JSON.stringify(await window.__shelterScrollProbe))()", timeout=20)
    if not raw:
        raise AcceptanceError("Long-page scroll sweep did not finish")
    return json.loads(raw)


def fast_scroll_probe(
    ui: Cdp,
    page: Cdp,
    process: subprocess.Popen,
    route: str,
    popup_during_scroll: bool,
) -> Dict[str, Any]:
    start = start_scroll_sweep(page)
    before = bridge_state(ui)
    if popup_during_scroll:
        time.sleep(0.08)
        ui.evaluate("document.getElementById('moreBtn').click()")
        wait_until(
            lambda: (lambda s: bool(s and s.get("frozen") and not s.get("busy")))(bridge_state(ui)),
            "menu opening during rapid page scrolling",
            process,
            timeout=15,
        )
        menu_style = ui.evaluate(
            "(function(){var m=document.querySelector('.menu:not(.closing)');if(!m)return null;"
            "var c=getComputedStyle(m);return JSON.stringify({background:c.backgroundColor,blur:c.backdropFilter,"
            "alpha:(function(v){var m=String(v).match(/\\/\\s*([0-9.]+)(%)?\\s*\\)$/);"
            "if(m)return m[2]?+m[1]/100:+m[1];if(/^rgba\\(/.test(v))return +v.slice(0,-1).split(',').pop();return 1;})(c.backgroundColor)});})()"
        )
        if not menu_style:
            raise AcceptanceError(f"Popup vanished during the scroll probe for {route}")
        menu_style = json.loads(menu_style)
        if menu_style.get("alpha", 0) < 0.939:
            raise AcceptanceError(f"Popup lost its opaque fallback during fast scrolling: {menu_style}")
        ui.evaluate("document.getElementById('moreBtn').click()")
        wait_native_overlay_idle(ui, process)
    scroll = finish_scroll_sweep(page)
    after = bridge_state(ui)
    if scroll.get("maxScroll", 0) < 10000 or scroll.get("finalY", 0) < scroll.get("maxScroll", 0) - 3:
        raise AcceptanceError(f"Fast scroll did not reach the end of {route}: {scroll}")
    layout_delta = int(after.get("layoutRequests", 0)) - int(before.get("layoutRequests", 0))
    if layout_delta > 1:
        raise AcceptanceError(
            f"Native page scrolling caused unnecessary viewport layouts on {route}: "
            f"delta={layout_delta}, before={before}, after={after}"
        )
    return {
        "route":route, "scroll":scroll, "popupDuringScroll":popup_during_scroll,
        "layoutRequestDelta":layout_delta, "menu":menu_style if popup_during_scroll else None
    }


def long_native_pages_probe(
    ui: Cdp,
    port: int,
    base_url: str,
    process: subprocess.Popen,
    site_pages: List[Cdp],
) -> List[Dict[str, Any]]:
    host = base_url.split("//", 1)[1]
    results = []
    for route in ("a", "b", "c"):
        url = f"{base_url}/long/{route}"
        ui.evaluate("window.newTab(" + json.dumps(url) + ")")
        _target, page, state = wait_for_site(
            port, host, process, timeout=30, path_contains=f"/long/{route}"
        )
        site_pages.append(page)
        if state.get("title") != f"SHELTER long-page {route}":
            raise AcceptanceError(f"Long-page fixture did not load {route}: {state}")
        results.append(
            fast_scroll_probe(
                ui, page, process, f"/long/{route}", popup_during_scroll=True
            )
        )
    return results


def loading_popup_probe(
    ui: Cdp,
    port: int,
    base_url: str,
    process: subprocess.Popen,
    site_pages: List[Cdp],
) -> Dict[str, Any]:
    host = base_url.split("//", 1)[1]
    url = f"{base_url}/long/loading"
    ui.evaluate("window.newTab(" + json.dumps(url) + ")")
    target = wait_until(
        lambda: find_site_page(port, host, path_contains="/long/loading"),
        "delayed native page target",
        process,
        timeout=15,
    )
    page = Cdp(target["webSocketDebuggerUrl"])
    page.call("Runtime.enable")
    page.call("Page.enable")
    site_pages.append(page)

    def still_loading():
        state = page_state(page)
        return state if "/long/loading" in state.get("url", "") and state.get("readyState") != "complete" else False

    loading_state = wait_until(
        still_loading, "native document still loading", process, timeout=5, interval=0.05
    )
    ui.evaluate("document.getElementById('moreBtn').click()")
    ui.evaluate("new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)))")
    menu_style = ui.evaluate(
        r"""(function(){
          var menu=document.querySelector('.menu:not(.closing)');
          if(!menu)return null;
          var color=getComputedStyle(menu).backgroundColor;
          var slash=color.match(/\/\s*([0-9.]+)(%)?\s*\)$/);
          var alpha=slash?(slash[2]?Number(slash[1])/100:Number(slash[1])):
            (/^rgba\(/i.test(color)?Number(color.slice(0,-1).split(',').pop()):1);
          return JSON.stringify({backgroundColor:color,alpha:alpha});
        })()"""
    )
    if not menu_style:
        raise AcceptanceError("Popup did not remain open over the still-loading native page")
    menu_style = json.loads(menu_style)
    if menu_style.get("alpha", 0) < 0.939:
        raise AcceptanceError(f"Popup fallback is too transparent during native loading: {menu_style}")
    loading_during_popup = page_state(page)
    if loading_during_popup.get("readyState") == "complete":
        raise AcceptanceError("The delayed fixture finished before the loading-popup probe ran")
    ui.evaluate("document.getElementById('moreBtn').click()")
    wait_native_overlay_idle(ui, process)

    def loaded_page():
        state = page_state(page)
        return state if state.get("readyState") == "complete" else False

    state = wait_until(loaded_page, "delayed native page completion", process, timeout=20)
    if "scrollable native document" not in state.get("text", "").lower():
        raise AcceptanceError(f"Page content is incomplete after popup open/close: {state}")
    return {
        "url":state.get("url"), "loadingState":loading_state.get("readyState"),
        "loadingDuringPopup":loading_during_popup.get("readyState"),
        "popup":menu_style, "title":state.get("title"), "target_id":target.get("id"),
        "bridge":bridge_state(ui)
    }


def page_state(page: Cdp) -> Dict[str, Any]:

    raw = page.evaluate(
        "JSON.stringify({url:location.href,title:document.title,"
        "readyState:document.readyState,"
        "text:(document.body&&document.body.innerText||'').slice(0,800)})"
    )
    return json.loads(raw) if raw else {}


def find_site_page(
    port: int, host: str, path_contains: Optional[str] = None
) -> Optional[Dict[str, Any]]:
    for target in get_targets(port):
        url = target.get("url", "").lower()
        if (
            target.get("type") == "page"
            and host.lower() in url
            and (not path_contains or path_contains.lower() in url)
            and target.get("webSocketDebuggerUrl")
        ):
            return target
    return None


def wait_for_site(
    port: int,
    host: str,
    process: subprocess.Popen,
    timeout: float = 45,
    path_contains: Optional[str] = None,
) -> Tuple[Dict[str, Any], Cdp, Dict[str, Any]]:
    target = wait_until(
        lambda: find_site_page(port, host, path_contains),
        f"native CEF page for {host}{path_contains or ''}",

        process,
        timeout,
    )
    page = Cdp(target["webSocketDebuggerUrl"])
    page.call("Runtime.enable")
    page.call("Page.enable")
    state: Dict[str, Any] = {}

    def loaded():
        nonlocal state
        state = page_state(page)
        return (
            host in state.get("url", "").lower()
            and state.get("readyState") == "complete"
            and bool(state.get("title") or state.get("text"))
        )

    try:
        wait_until(loaded, f"document content from {host}", process, timeout)
    except Exception:
        page.close()
        raise
    return target, page, state


def stop_process(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        process.terminate()
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=4)


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: acceptance_smoke.py <packaged-shelter-executable>", file=sys.stderr)
        return 2

    executable = Path(sys.argv[1]).expanduser().resolve()
    if not executable.is_file():
        print(f"Packaged executable not found: {executable}", file=sys.stderr)
        return 2

    port = reserve_port()
    command = [
        str(executable),
        f"--remote-debugging-port={port}",
        "--remote-allow-origins=*",
    ]
    log_path = Path.cwd() / "acceptance-app.log"
    process = None
    ui = None
    site_pages: List[Cdp] = []
    long_page_server: Optional[ThreadingHTTPServer] = None
    long_page_thread: Optional[threading.Thread] = None
    long_page_base = ""
    original_gfx = "balance"

    try:
        long_page_server, long_page_thread, long_page_base = start_long_page_server()
        print(f"SHELTER_ACCEPTANCE_VERSION expected=1.0.165", flush=True)
        print(f"SHELTER_ACCEPTANCE_EXECUTABLE {executable}", flush=True)
        with log_path.open("w", encoding="utf-8") as app_log:
            process = subprocess.Popen(
                command,
                cwd=str(executable.parent),
                stdout=app_log,
                stderr=subprocess.STDOUT,
            )
            ui_target = wait_until(
                lambda: next(
                    (
                        target
                        for target in get_targets(port)
                        if target.get("type") == "page"
                        and target.get("url", "").startswith("shelter://app/")
                        and target.get("webSocketDebuggerUrl")
                    ),
                    None,
                ),
                "the packaged SHELTER UI target",
                process,
                timeout=90,
            )
            ui = Cdp(ui_target["webSocketDebuggerUrl"])
            ui.call("Runtime.enable")
            ui.call("Page.enable")
            wait_until(
                lambda: ui.evaluate(
                    "typeof window.getActiveTabId === 'function' && "
                    "typeof window.openPage === 'function' && "
                    "!!document.getElementById('viewport')"
                )
                is True,
                "the UI bridge and dashboard shell",
                process,
                timeout=45,
            )
            version = ui.evaluate("window.shelter && window.shelter.version")
            if version != "1.0.165":
                raise AcceptanceError(
                    f"Packaged UI version mismatch: expected 1.0.165, got {version!r}"
                )
            print(f"SHELTER_ACCEPTANCE_UI_READY version={version}", flush=True)
            original_gfx = ui.evaluate("window.shelterTest.state().prefs.gfx || 'balance'")
            if original_gfx not in ("beauty", "balance", "perf"):
                original_gfx = "balance"

            ui.evaluate("window.openPage('dashboard')")
            wait_until(
                lambda: ui.evaluate("!!document.querySelector('.dash-top')") is True,
                "the dashboard page",
                process,
                timeout=20,
            )
            measurements: List[Dict[str, Any]] = []
            for width in (1280, 997, 768, 390):
                ui.call(
                    "Emulation.setDeviceMetricsOverride",
                    {
                        "width": width,
                        "height": 900,
                        "deviceScaleFactor": 1,
                        "mobile": False,
                        "screenWidth": width,
                        "screenHeight": 900,
                    },
                )
                time.sleep(0.25)
                measurement = viewport_measurement(ui)
                if not measurement:
                    raise AcceptanceError(
                        f"Dashboard geometry unavailable at window width {width}px"
                    )
                measurements.append(measurement)
                print(
                    "SHELTER_ACCEPTANCE_DASHBOARD "
                    + json.dumps(measurement, ensure_ascii=False),
                    flush=True,
                )
                if float(measurement.get("overflowPx", 0)) > 1:
                    raise AcceptanceError(
                        "Dashboard cards overflow at window width "
                        f"{width}px: {measurement}"
                    )
            ui.call("Emulation.clearDeviceMetricsOverride")
            print(
                "SHELTER_ACCEPTANCE_DASHBOARD_PASS "
                + json.dumps(measurements, ensure_ascii=False),
                flush=True,
            )

            sidebar_measurements: List[Dict[str, Any]] = []
            for width in (1280, 390):
                measurement = sidebar_scroll_probe(ui, width, 1100)
                sidebar_measurements.append(measurement)
                print(
                    "SHELTER_ACCEPTANCE_SIDEBAR "
                    + json.dumps(measurement, ensure_ascii=False),
                    flush=True,
                )
                empty = measurement["empty"]
                one_tab = measurement["oneTab"]
                many_tabs = measurement["manyTabs"]
                at_bottom = measurement["atBottom"]
                collapsed = measurement["collapsed"]
                if one_tab["clientHeight"] <= empty["clientHeight"] + 20:
                    raise AcceptanceError(
                        "Sidebar scrollport did not grow with one added tab at "
                        f"{width}px: {measurement}"
                    )
                if abs(collapsed["clientHeight"] - empty["clientHeight"]) > 2:
                    raise AcceptanceError(
                        "Sidebar scrollport did not shrink after removing tabs at "
                        f"{width}px: {measurement}"
                    )
                if many_tabs["scrollHeight"] <= many_tabs["clientHeight"] + 20:
                    raise AcceptanceError(
                        "Sidebar did not become scrollable with a long tab list at "
                        f"{width}px: {measurement}"
                    )
                if abs(at_bottom["scrollTop"] - at_bottom["maxScroll"]) > 2:
                    raise AcceptanceError(
                        "Sidebar probe did not reach the actual scroll limit at "
                        f"{width}px: {measurement}"
                    )
                for state_name, state in (
                    ("empty", empty),
                    ("oneTab", one_tab),
                    ("atBottom", at_bottom),
                    ("collapsed", collapsed),
                ):
                    if abs(state["gapAfterExtensionsPx"]) > 1.5:
                        raise AcceptanceError(
                            "Empty scroll space remains after Extensions "
                            f"({state_name}, {width}px): {measurement}"
                        )
            ui.call("Emulation.clearDeviceMetricsOverride")
            print(
                "SHELTER_ACCEPTANCE_SIDEBAR_PASS "
                + json.dumps(sidebar_measurements, ensure_ascii=False),
                flush=True,
            )

            for width in (1280, 390):
                ui.call(
                    "Emulation.setDeviceMetricsOverride",
                    {
                        "width": width,
                        "height": 900,
                        "deviceScaleFactor": 1,
                        "mobile": False,
                        "screenWidth": width,
                        "screenHeight": 900,
                    },
                )
                try:
                    toolbar_menu = toolbar_menu_measurement(ui)
                finally:
                    ui.call("Emulation.clearDeviceMetricsOverride")
                print(
                    "SHELTER_ACCEPTANCE_TOOLBAR_MENU "
                    + json.dumps(toolbar_menu, ensure_ascii=False),
                    flush=True,
                )
                if not toolbar_menu.get("dashboardFallbackDisabled"):
                    raise AcceptanceError(
                        f"Native-page popup fallback leaked onto the Express/dashboard UI: {toolbar_menu}"
                    )
                if not all(toolbar_menu["toolbar"].values()):
                    raise AcceptanceError(
                        f"Toolbar buttons are misplaced at {width}px: {toolbar_menu}"
                    )
                if toolbar_menu["visibleForbidden"]:
                    raise AcceptanceError(
                        f"Removed actions remain in the burger menu: {toolbar_menu}"
                    )
                if not toolbar_menu["profileLast"]:
                    raise AcceptanceError(
                        "Profile with avatar is not the last burger-menu item: "
                        f"{toolbar_menu}"
                    )
                palette = toolbar_menu["palette"]
                if not palette["immediatelyAfterOpacity"] or palette["swatchCount"] != 6 or not palette["equalSideInsets"]:
                    raise AcceptanceError(
                        f"Theme palette is not directly after opacity: {toolbar_menu}"
                    )
                print(
                    "SHELTER_ACCEPTANCE_TOOLBAR_MENU_PASS "
                    + json.dumps(toolbar_menu, ensure_ascii=False),
                    flush=True,
                )

            theme_probe = quick_theme_switch_probe(ui)
            print("SHELTER_ACCEPTANCE_QUICK_THEME " + json.dumps(theme_probe, ensure_ascii=False), flush=True)
            print("SHELTER_ACCEPTANCE_QUICK_THEME_PASS", flush=True)

            mode_probe = performance_mode_probe(ui)
            print("SHELTER_ACCEPTANCE_PERFORMANCE " + json.dumps(mode_probe, ensure_ascii=False), flush=True)
            print("SHELTER_ACCEPTANCE_PERFORMANCE_PASS", flush=True)
            # Clear toasts generated by the acceptance-only segmented-control clicks.
            ui.evaluate("document.querySelectorAll('#toasts>.toast').forEach(el=>el.remove())")

            ui.evaluate("window.newTab('https://example.com/')")
            target, page, state = wait_for_site(port, "example.com", process)
            site_pages.append(page)
            if "Example Domain" not in (
                state.get("title", "") + " " + state.get("text", "")
            ):
                raise AcceptanceError(
                    f"CEF reached {state.get('url')} but rendered unexpected content: "
                    f"{state.get('title')!r} {state.get('text', '')[:160]!r}"
                )
            print(
                "SHELTER_ACCEPTANCE_SITE_PASS "
                + json.dumps(
                    {
                        "url": state.get("url"),
                        "title": state.get("title"),
                        "body": state.get("text", "")[:180],
                        "target_id": target.get("id"),
                    },
                    ensure_ascii=False,
                ),
                flush=True,
            )

            popup_probe = popup_fallback_probe(ui, process)
            print("SHELTER_ACCEPTANCE_NATIVE_POPUP " + json.dumps(popup_probe, ensure_ascii=False), flush=True)
            print("SHELTER_ACCEPTANCE_NATIVE_POPUP_PASS", flush=True)

            ui.evaluate("window.navigate('https://example.org/')")
            target, page, state = wait_for_site(port, "example.org", process)
            site_pages.append(page)
            if "Example Domain" not in (
                state.get("title", "") + " " + state.get("text", "")
            ):
                raise AcceptanceError(
                    f"Navigation reached {state.get('url')} but page content is missing"
                )
            print(
                "SHELTER_ACCEPTANCE_NAVIGATION_PASS "
                + json.dumps(
                    {"url": state.get("url"), "title": state.get("title")},
                    ensure_ascii=False,
                ),
                flush=True,
            )

            ui.evaluate(
                "(function(){var b=document.getElementById('btnBack');"
                "if(!b) throw new Error('back button is missing'); b.click(); return true;})()"
            )
            target, page, state = wait_for_site(port, "example.com", process)
            site_pages.append(page)
            if "example.com" not in state.get("url", "").lower():
                raise AcceptanceError(
                    f"Back navigation did not restore example.com: {state}"
                )
            print(
                "SHELTER_ACCEPTANCE_HISTORY_BACK_PASS "
                + json.dumps({"url": state.get("url")}, ensure_ascii=False),
                flush=True,
            )

            # Exercise the real native BrowserView on three long, differently
            # colored pages while Visual mode remains enabled.
            ui.evaluate("window.shelterTest.setGfxMode('beauty',{persist:false,notify:false})")
            ui.evaluate("document.querySelectorAll('#toasts>.toast').forEach(el=>el.remove())")
            loading_probe = loading_popup_probe(
                ui, port, long_page_base, process, site_pages
            )
            print("SHELTER_ACCEPTANCE_LOADING_POPUP " + json.dumps(loading_probe, ensure_ascii=False), flush=True)
            print("SHELTER_ACCEPTANCE_LOADING_POPUP_PASS", flush=True)

            scroll_probes = long_native_pages_probe(
                ui, port, long_page_base, process, site_pages
            )
            print("SHELTER_ACCEPTANCE_LONG_SCROLL " + json.dumps(scroll_probes, ensure_ascii=False), flush=True)
            print("SHELTER_ACCEPTANCE_LONG_SCROLL_PASS", flush=True)
            ui.evaluate(
                "window.shelterTest.setGfxMode(" + json.dumps(original_gfx) +
                ",{persist:false,notify:false})"
            )

            print("SHELTER_ACCEPTANCE_PASS", flush=True)
        return 0
    except Exception as exc:
        print(f"SHELTER_ACCEPTANCE_FAIL {exc}", file=sys.stderr, flush=True)
        if process is not None and process.poll() is not None:
            print(
                f"SHELTER_ACCEPTANCE_APP_EXIT status={process.returncode}",
                file=sys.stderr,
                flush=True,
            )
        if log_path.is_file():
            try:
                tail = log_path.read_text(encoding="utf-8", errors="replace")[-8000:]
                if tail:
                    print("--- packaged app output ---", file=sys.stderr)
                    print(tail, file=sys.stderr)
            except OSError:
                pass
        return 1
    finally:
        if ui is not None:
            ui.close()
        for page in site_pages:
            page.close()
        if process is not None:
            stop_process(process)
        if long_page_server is not None:
            long_page_server.shutdown()
            long_page_server.server_close()
        if long_page_thread is not None:
            long_page_thread.join(timeout=2)


if __name__ == "__main__":
    raise SystemExit(main())
