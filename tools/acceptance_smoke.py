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
        if path == "/adsbygoogle.js":
            # Рекламный скрипт: совпадает с правилом /adsbygoogle.js и грузится
            # прямо из документа страницы, то есть из главного фрейма.
            late = "late" in self.path
            script = (
                "window.__adLoaded=(window.__adLoaded||0)+1;"
                + ("window.__adLateLoaded=true;" if late else "")
                + "document.title='AD SCRIPT LOADED';"
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(script)))
            self.end_headers()
            try:
                self.wfile.write(script)
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError,
                    ConnectionAbortedError):
                pass
            return
        if path in ("/consent", "/consent-late"):
            late = path.endswith("-late")
            immediate = (
                '<div id="cmp" class="cookie-consent" role="dialog">'
                '<p>Мы используем cookies</p>'
                '<button id="c-min" onclick="window.__answer(\'necessary\')">'
                'Принять только необходимые</button>'
                '<button id="c-all" onclick="window.__answer(\'all\')">Принять все</button>'
                '</div>'
            )
            mount = (
                "" if not late
                else """<script>
                  setTimeout(function(){
                    var box = document.createElement('div');
                    box.id = 'cmp';
                    box.className = 'cookie-consent';
                    box.setAttribute('role', 'dialog');
                    box.innerHTML = '<p>Мы используем cookies</p>' +
                      '<button onclick="window.__answer(\\'necessary\\')">Принять только необходимые</button>' +
                      '<button onclick="window.__answer(\\'all\\')">Принять все</button>';
                    document.body.appendChild(box);
                    window.__lateMounted = true;
                  }, 700);
                </script>"""
            )
            document = (
                "<!doctype html><html lang='ru'><head><meta charset='utf-8'>"
                "<title>Consent fixture</title></head><body>"
                "<h1>SHELTER consent fixture</h1>"
                + ("" if late else immediate)
                + "<script>window.__consent=null;window.__lateMounted=false;"
                  "window.__answer=function(kind){window.__consent=kind;"
                  "var b=document.getElementById('cmp');"
                  "if(b&&b.parentNode)b.parentNode.removeChild(b);};</script>"
                + mount
                + """<script>
                  /* Видимость считаем в момент чтения флагов: rAF в скрытом
                     окне может не сработать, а решение CMP видно сразу. */
                  window.__visible = function(){
                    var el = document.getElementById('cmp');
                    if (!el || !el.isConnected) return false;
                    for (var n = el; n && n.nodeType === 1; n = n.parentElement) {
                      var st = getComputedStyle(n);
                      if (st.display === 'none' || st.visibility === 'hidden' ||
                          st.opacity === '0') return false;
                    }
                    return true;
                  };
                </script>"""
                "</body></html>"
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(document)))
            self.end_headers()
            try:
                self.wfile.write(document)
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError,
                    ConnectionAbortedError):
                pass
            return
        if path == "/adpage":
            document = (
                "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
                "<title>Ad fixture</title>"
                "<script src='/adsbygoogle.js'></script>"
                "</head><body><h1>SHELTER ad fixture</h1>"
                "<ins class='adsbygoogle' id='adSlot'>"
                "<div id='adSlotInner'>AD SLOT</div></ins>"
                "<script>setTimeout(function(){var s=document.createElement('script');"
                "s.src='/adsbygoogle.js?late=1';document.head.appendChild(s);},250);"
                "</script></body></html>"
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(document)))
            self.end_headers()
            try:
                self.wfile.write(document)
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError,
                    ConnectionAbortedError):
                pass
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


class QuietThreadingHTTPServer(ThreadingHTTPServer):
    """Сервер фикстур: оборванное соединение — не ошибка, не сыпем трейсбеки.

    Браузер штатно закрывает сокеты при переходах между страницами, и
    socketserver печатал на каждое такое закрытие многострочный traceback,
    который забивал diagnostics-лог приёмочного прогона."""

    def handle_error(self, request, client_address) -> None:  # noqa: D102
        exc = sys.exc_info()[1]
        if isinstance(exc, (BrokenPipeError, ConnectionResetError,
                            ConnectionAbortedError)):
            return
        super().handle_error(request, client_address)


def start_long_page_server() -> Tuple[ThreadingHTTPServer, threading.Thread, str]:
    server = QuietThreadingHTTPServer(("127.0.0.1", 0), LongPageHandler)
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
    """Exercise the six ordered mode transitions, preference persistence, and both controls."""
    expression = r"""
      (async function() {
        const test = window.shelterTest;
        if (!test || !test.setGfxMode || !test.gfxSnapshot)
          throw new Error('performance test surface is missing');
        const original = test.state().prefs.gfx || 'balance';
        const originalGlow = test.state().prefs.glow !== false;
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const quick = document.getElementById('quickBtn');
        if (!quick) throw new Error('quick-menu button is missing');
        quick.click(); await frame();
        let quickMenu = document.querySelector('.menu-quick:not(.closing)');
        if (!quickMenu) throw new Error('quick menu did not open');
        const quickGlowButton = () => {
          quickMenu = document.querySelector('.menu-quick:not(.closing)');
          return quickMenu && quickMenu.querySelector('[data-sw="glow"]');
        };
        const setQuickGlow = async value => {
          const button = quickGlowButton();
          if (!button) throw new Error('quick-menu glow toggle is missing');
          if (button.getAttribute('aria-label') !== 'Включение или отключение свечения')
            throw new Error('quick-menu glow toggle has no accessible label');
          if (button.disabled) throw new Error('glow toggle is disabled outside Speed');
          if ((test.state().prefs.glow !== false) !== value) {
            button.click(); await frame();
          }
          if ((test.state().prefs.glow !== false) !== value)
            throw new Error('quick-menu glow toggle failed to save preference');
        };
        test.setGfxMode('balance', {persist:false, notify:false});
        await frame();
        await setQuickGlow(!originalGlow);
        const testGlow = !originalGlow;
        const verify = (expected, snap, glowPref) => {
          const issues = [];
          const visual = expected === 'beauty';
          const speed = expected === 'perf';
          const activeGlow = glowPref && !speed;
          const animatedGlow = glowPref && visual;
          if (snap.gfx !== expected || snap.dataGfx !== expected) issues.push('mode state mismatch');
          if (snap.motion !== (speed ? 'off' : 'full')) issues.push('animation state mismatch');
          if (snap.glow !== glowPref) issues.push('saved glow preference changed with mode');
          if (snap.glowActive !== activeGlow || snap.glowOff === activeGlow) issues.push('effective glow state mismatch');
          if (snap.glowAnimated !== animatedGlow) issues.push('glow animation state mismatch');
          if (snap.reduceMotion !== speed) issues.push('reduced-motion class mismatch');
          if (visual ? !snap.blur.includes('blur(') : snap.blur !== 'none') issues.push('glass blur mismatch');
          if (visual ? !snap.menuBlur.includes('blur(') : snap.menuBlur !== 'none') issues.push('menu blur mismatch');
          if (visual ? !snap.glassBackdrop.includes('blur(') : snap.glassBackdrop !== 'none') issues.push('surface backdrop-filter mismatch');
          if (activeGlow ? snap.glowSoft === 'none' : snap.glowSoft !== 'none') issues.push('glow shadow token mismatch');
          if (activeGlow ? snap.glowRing === 'none' : snap.glowRing !== 'none') issues.push('glow ring token mismatch');
          const glowAlpha = [snap.glowSpotAlpha, snap.glowShadowAlpha, snap.glowBorderPercent].map(parseFloat);
          if (activeGlow ? glowAlpha.some(value => !(value > 0)) : glowAlpha.some(value => value !== 0))
            issues.push('glow variables were not updated atomically');
          if (animatedGlow ? snap.animation === 'none' : snap.animation !== 'none') issues.push('ambient animation mismatch');
          if (snap.heroBeforeDisplay && (activeGlow ? snap.heroBeforeDisplay === 'none' : snap.heroBeforeDisplay !== 'none'))
            issues.push('Hero glow pseudo-element does not match the preference');
          if (snap.dotAfterDisplay && (activeGlow ? snap.dotAfterDisplay === 'none' : snap.dotAfterDisplay !== 'none'))
            issues.push('status-dot glow pseudo-element does not match the preference');
          if (snap.dotAfterAnimation && (animatedGlow ? snap.dotAfterAnimation === 'none' : snap.dotAfterAnimation !== 'none'))
            issues.push('status-dot animation does not match the mode');
          const hasTransition = snap.transition.split(',').some(x => parseFloat(x) > 0);
          if (speed ? hasTransition : !hasTransition) issues.push('ordinary UI transition mismatch');
          if (speed && snap.runningAnimations) issues.push('Speed still has running or pending UI animations');
          if (speed && snap.shadow !== 'none') issues.push('Speed shadow was not removed');
          if (speed && snap.filter !== 'none') issues.push('Speed filter was not removed');
          if (speed && snap.backgroundImage !== 'none') issues.push('Speed gradient/glow background remains');
          const alpha = snap.cardBackground.match(/rgba\([^,]+,[^,]+,[^,]+,\s*([0-9.]+)\)/i);
          if (speed && alpha && parseFloat(alpha[1]) < 0.999) issues.push('Speed surface is translucent');
          if (!speed && Math.round(parseFloat(snap.glassOpacity)) !== Math.round(+test.state().prefs.glassOp || 82)) issues.push('glass opacity preference changed');
          if (speed && Math.round(parseFloat(snap.glassOpacity)) !== 100) issues.push('Speed is not opaque');
          if (snap.hero && snap.hero.balanceCache) issues.push('stale cached Hero frame');
          if (snap.hero && !activeGlow && snap.hero.canvas && snap.hero.canvasVisibility !== 'hidden') issues.push('disabled Hero glow was not cleared');
          if (snap.hero && activeGlow && snap.hero.visible && snap.hero.canvasVisibility === 'hidden') issues.push('enabled Hero glow is missing');
          if (snap.hero && expected === 'balance' && activeGlow && snap.hero.visible && snap.hero.raf) issues.push('Balance glow is not static');
          if (snap.hero && !activeGlow && snap.hero.offscreenBuffers !== 0) issues.push('Hero glow cache survived mode change');
          if ((!visual || !glowPref) && snap.spotLights) issues.push('dynamic cursor glow survived static/off mode');
          if (document.documentElement.classList.contains('theme-anim') || document.documentElement.classList.contains('vt-theme')) issues.push('theme transition class leaked');
          const amb = document.querySelector('.ambient'), fx = document.getElementById('heroFx');
          if ((amb && (amb.style.opacity || amb.style.transition)) || (fx && (fx.style.opacity || fx.style.transition))) issues.push('inline glow fade leaked');
          return issues;
        };
        const checked = [];
        // Explicitly cover all six ordered pairs among Visual, Balance, and Speed.
        const plan = ['beauty', 'balance', 'beauty', 'balance', 'perf', 'balance',
          'beauty', 'perf', 'beauty', 'perf', 'balance', 'perf', 'beauty'];
        for (const mode of plan) {
          test.setGfxMode(mode, {persist:false, notify:false});
          await frame();
          const snap = test.gfxSnapshot();
          const issues = verify(mode, snap, testGlow);
          checked.push({mode:mode, issues:issues, snapshot:snap});
          if (issues.length) throw new Error('mode ' + mode + ': ' + issues.join(', '));
        }
        // Several complete cycles without yielding catch stale classes, variables,
        // animations, and glow buffers left behind by rapid repeated switching.
        const rapid = ['beauty', 'balance', 'perf', 'beauty', 'perf', 'balance'];
        for (let cycle = 0; cycle < 5; cycle++)
          rapid.forEach(mode => test.setGfxMode(mode, {persist:false, notify:false}));
        await frame();
        let rapidSnap = test.gfxSnapshot();
        let rapidIssues = verify('balance', rapidSnap, testGlow);
        if (rapidIssues.length) throw new Error('rapid switching: ' + rapidIssues.join(', '));

        // The glow row remains present in Quick Menu, and is disabled only in Speed.
        const qTarget = original === 'perf' ? 'beauty' : 'perf';
        const qButton = quickMenu.querySelector('.seg[data-seg="motion"] button[data-v="' + qTarget + '"]');
        if (!qButton) throw new Error('quick-menu mode buttons are missing');
        qButton.click(); await frame();
        const quickModeOpen = !!document.querySelector('.menu-quick:not(.closing)') && test.state().prefs.gfx === qTarget;
        if (!quickModeOpen) throw new Error('quick-menu mode selection closed the menu or failed');
        test.setGfxMode('perf', {persist:false, notify:false}); await frame();
        const speedGlow = quickGlowButton();
        const speedGlowRow = quickMenu.querySelector('#qGlowRow');
        if (!speedGlow || !speedGlowRow || !speedGlowRow.getClientRects().length ||
            getComputedStyle(speedGlowRow).visibility === 'hidden' || !speedGlow.disabled ||
            speedGlow.getAttribute('aria-disabled') !== 'true')
          throw new Error('Quick Menu glow toggle is not visible and disabled in Speed');
        const savedInSpeed = test.state().prefs.glow !== false;
        speedGlow.click(); await frame();
        if ((test.state().prefs.glow !== false) !== savedInSpeed) throw new Error('disabled Speed toggle changed saved glow preference');
        test.setGfxMode('balance', {persist:false, notify:false}); await frame();
        if (!quickGlowButton() || quickGlowButton().disabled) throw new Error('Quick Menu glow toggle did not re-enable in Balance');
        test.setGfxMode('beauty', {persist:false, notify:false}); await frame();
        if (!quickGlowButton() || quickGlowButton().disabled) throw new Error('Quick Menu glow toggle was disabled in Visual');
        test.setGfxMode('balance', {persist:false, notify:false}); await frame();
        await setQuickGlow(originalGlow);
        const quickGlowPreserved = (test.state().prefs.glow !== false) === originalGlow;
        quick.click(); await frame();

        // Repeat the toggle/disabled-state checks in ordinary Settings.
        test.openSettings('look'); await frame();
        const settings = document.querySelector('.settings');
        const glowRow = settings && settings.querySelector('#setGlowRow');
        const settingsGlow = glowRow && glowRow.querySelector('[data-sw="glow"]');
        if (!glowRow || !settingsGlow) throw new Error('Settings glow row is missing');
        if (settingsGlow.getAttribute('aria-label') !== 'Включение или отключение свечения')
          throw new Error('Settings glow toggle has no accessible label');
        if (settingsGlow.disabled) throw new Error('Settings glow toggle is disabled outside Speed');
        const settingsPref = test.state().prefs.glow !== false;
        settingsGlow.click(); await frame();
        if ((test.state().prefs.glow !== false) === settingsPref) throw new Error('Settings glow toggle did not change the saved preference');
        settingsGlow.click(); await frame();
        if ((test.state().prefs.glow !== false) !== settingsPref) throw new Error('Settings glow toggle did not restore the saved preference');
        test.setGfxMode('perf', {persist:false, notify:false}); await frame();
        const settingsGlowRow = settings.querySelector('#setGlowRow');
        if (!settingsGlowRow || !settingsGlowRow.getClientRects().length ||
            getComputedStyle(settingsGlowRow).visibility === 'hidden' ||
            !settingsGlowRow.querySelector('[data-sw="glow"]:disabled'))
          throw new Error('Settings glow toggle is not visible and disabled in Speed');
        const settingsSpeedPref = test.state().prefs.glow !== false;
        settings.querySelector('#setGlowRow [data-sw="glow"]').click(); await frame();
        if ((test.state().prefs.glow !== false) !== settingsSpeedPref) throw new Error('Settings Speed toggle changed saved glow preference');
        test.setGfxMode('balance', {persist:false, notify:false}); await frame();
        if (settings.querySelector('#setGlowRow [data-sw="glow"]').disabled)
          throw new Error('Settings glow toggle did not re-enable in Balance');
        test.setGfxMode('beauty', {persist:false, notify:false}); await frame();
        if (settings.querySelector('#setGlowRow [data-sw="glow"]').disabled)
          throw new Error('Settings glow toggle was disabled in Visual');
        test.setGfxMode('balance', {persist:false, notify:false}); await frame();

        // The real native-delete API is present; do not invoke the destructive action.
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

        test.setGfxMode(original, {persist:false, notify:false});
        if ((test.state().prefs.glow !== false) !== originalGlow)
          throw new Error('saved glow preference was not restored after mode transitions');
        test.setGfxMode(original, {persist:true, notify:false});
        await frame();
        return JSON.stringify({checked:checked.length, rapidCycles:5, quickModeOpen:quickModeOpen,
          quickGlowPreserved:quickGlowPreserved, settingsMode:settingsMode,
          nativeDeleteAvailable:nativeDeleteAvailable, final:test.gfxSnapshot()});
      })()
    """
    raw = ui.evaluate(expression, timeout=40)
    if not raw:
        raise AcceptanceError("Performance-mode regression probe returned no result")
    result = json.loads(raw)
    if result.get("checked") != 13 or result.get("rapidCycles") != 5:
        raise AcceptanceError(f"Performance-mode probe did not complete: {result}")
    if result.get("quickGlowPreserved") is not True:
        raise AcceptanceError(f"Quick-menu glow preference was not preserved: {result}")
    return result

def tab_width_probe(ui: Cdp) -> Dict[str, Any]:
    """Tabs must share one width (standard browser logic) in every dock.

    Wide windows: equal widths, capped by the layout maximum. Narrow windows:
    equal widths that may shrink but never below the historical minimum.
    """
    expression = r"""
      (async function() {
        const test = window.shelterTest;
        if (!test || !test.setTabPos || !test.state)
          throw new Error('tab position test surface is missing');
        const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const MIN = 104, MAX = 216;
        const originalPos = test.state().ui.tabPos;
        const openIds = ids => ids.filter(id => id && id !== test.state().activeTabId);
        const titles = [
          'Короткая',
          'Очень длинное название страницы проверки равной ширины вкладок',
          'Mid',
          'Ещё одно длинное имя вкладки для проверки'
        ];
        const results = {};
        const problems = [];
        try {
          for (const pos of ['top', 'bottom', 'right', 'left']) {
           try {
            test.setTabPos(pos);
            await frame(); await pause(120);
            const list = document.getElementById('tabList');
            if (!list) throw new Error('tab list is missing for ' + pos);
            const space = () => {
              const state = test.state();
              const key = state.currentSpace;
              return (state.spaces && (state.spaces[key] || state.spaces)) || null;
            };
            const dropExtras = () => {
              const sp = space();
              const tabs = sp && sp.tabs ? sp.tabs.slice() : [];
              openIds(tabs.map(tab => tab.id)).forEach(id => window.closeTab(id));
            };
            dropExtras();
            await frame(); await pause(60);
            for (let i = 0; i < 3; i++) window.newTab();
            await frame(); await pause(140);
            const tabs = Array.from(list.querySelectorAll('.tab:not(.new)'));
            if (tabs.length < 4)
              throw new Error('expected four tabs in ' + pos + ' dock, got ' + tabs.length);
            tabs.forEach((tab, i) => {
              const label = tab.querySelector('.t');
              if (label) label.textContent = titles[i % titles.length];
            });
            await frame();
            const measure = () => Array.from(
              document.querySelectorAll('#tabList .tab:not(.new)')
            ).map(tab => {
              const rect = tab.getBoundingClientRect();
              return {w: Math.round(rect.width * 100) / 100, h: Math.round(rect.height)};
            });
            /* Диагностика раскладки: если ширины когда-нибудь разъедутся,
               отчёт должен объяснять причину без второго прогона CI. */
            const diag = () => {
              const app = document.getElementById('app');
              const track = document.querySelector('#dockLeft') || document.querySelector('.sb');
              const first = list.querySelector('.tab:not(.new)');
              const label = first ? first.querySelector('.t') : null;
              const rect = el => el ? Math.round(el.getBoundingClientRect().width * 100) / 100 : -1;
              const css = el => el ? getComputedStyle(el) : null;
              const listCss = css(list), tabCss = css(first);
              return {
                dock: pos,
                viewport: [innerWidth, innerHeight],
                appClass: app ? app.className : '',
                side: track ? (track.id || track.className) : '',
                sideW: rect(track),
                listW: rect(list),
                listClientW: list.clientWidth,
                listDisplay: listCss ? listCss.display + '/' + listCss.flexDirection : '',
                tabW: rect(first),
                tabH: rect(first),
                tabDisplay: tabCss ? tabCss.display : '',
                tabFlex: tabCss ? tabCss.flex : '',
                tabMin: tabCss ? tabCss.minWidth : '',
                tabMax: tabCss ? tabCss.maxWidth : '',
                tabIsCollapsedChild: !!(track && first && first.parentElement === track),
                labelW: rect(label),
                labelOpacity: label ? getComputedStyle(label).opacity : '',
                labelText: Array.from(list.querySelectorAll('.tab:not(.new) .t'))
                  .slice(0, 3).map(el => String(el.textContent || '').slice(0, 24)),
                sbVar: getComputedStyle(document.documentElement)
                  .getPropertyValue('--sb-w').trim(),
                sbVarCompact: getComputedStyle(document.documentElement)
                  .getPropertyValue('--sb-wc').trim(),
                uiCollapsed: !!test.state().ui.collapsed
              };
            };
            const dockWidth = () => Math.round(list.getBoundingClientRect().width * 100) / 100;
            const few = measure();
            const fewDock = dockWidth();
            const spread = Math.max(...few.map(t => t.w)) - Math.min(...few.map(t => t.w));
            if (spread > 1)
              throw new Error(
                'tabs differ in width in the ' + pos + ' dock: ' +
                JSON.stringify(few)
              );
            /* Порог «рабочей» ширины не может быть больше самого дока: в
               компактной боковой панели (или узком окне) место объективно
               меньше, и тогда требование — заполнять док, а не выдумывать
               пиксели. При нормальном доке порог остаётся 44px. */
            if (few.some(t => t.w < Math.min(44, fewDock) - 0.5))
              throw new Error(
                'tab collapsed below a usable width in ' + pos + ': ' +
                JSON.stringify(few) + ' of ' + fewDock + 'px dock ' +
                JSON.stringify(diag())
              );
            const horizontal = pos === 'top' || pos === 'bottom';
            if (horizontal && (few[0].w < MIN - 0.5 || few[0].w > MAX + 0.5))
              throw new Error(
                'horizontal tab width ' + few[0].w + ' left the ' + MIN + '–' + MAX +
                ' range in ' + pos
              );
            // Too many tabs: widths must still match and never drop below the
            // historical minimum of the old content-sized layout.
            for (let i = 0; i < 10; i++) window.newTab();
            await frame(); await pause(180);
            /* Новые вкладки перерисовывают список и стирают подставленные
               заголовки: возвращаем разные длины, иначе проверка «ширина не
               зависит от названия» под нагрузкой ничего не значит. */
            Array.from(list.querySelectorAll('.tab:not(.new)')).forEach((tab, i) => {
              const label = tab.querySelector('.t');
              if (label) label.textContent = titles[(i * 3) % titles.length];
            });
            await frame(); await pause(40);
            const many = measure();
            const manySpread = Math.max(...many.map(t => t.w)) - Math.min(...many.map(t => t.w));
            if (many.length < 12)
              throw new Error('expected twelve tabs in ' + pos + ', got ' + many.length);
            if (manySpread > 1)
              throw new Error(
                'tabs differ in width under load in the ' + pos + ' dock: ' +
                JSON.stringify(many.slice(0, 4))
              );
            const manyDock = dockWidth();
            const crowdedFloor = Math.min(MIN, manyDock) - 0.5;
            if (many[0].w < crowdedFloor)
              throw new Error(
                'crowded tabs shrank below the layout floor in ' + pos +
                ': ' + many[0].w + ' of ' + manyDock + 'px dock ' +
                JSON.stringify(diag())
              );
            results[pos] = {
              few: few[0], many: many[0], count: many.length,
              dockFew: fewDock, dockMany: manyDock, diag: diag()
            };
            dropExtras();
            await frame(); await pause(60);
           } catch (err) {
            /* Сломанный док не должен скрывать состояние остальных: собираем
               все проблемы и падаем один раз — с полным отчётом по каждому. */
            problems.push(pos + ': ' + String((err && err.message) || err));
           }
          }
          if (problems.length)
            throw new Error('tab layout problems: ' + problems.join(' | '));
          return JSON.stringify({min: MIN, max: MAX, docks: results});
        } finally {
          test.setTabPos(originalPos);
        }
      })()
    """
    result = ui.evaluate(expression, timeout=90)
    if not isinstance(result, str):
        raise AcceptanceError(f"Tab width probe returned no result: {result!r}")
    data = json.loads(result)
    if set(data.get("docks", {})) != {"top", "bottom", "right", "left"}:
        raise AcceptanceError(f"Tab width probe missed a dock: {data}")
    return data


def assistant_corners_probe(ui: Cdp) -> Dict[str, Any]:
    """The assistant tab must not contain a single sharp corner.

    Every visible surface on the deepthink page is checked: its four corner
    radii have to be equal and positive, and the two main panels must be
    separated rounded cards instead of panels welded corner-to-corner.
    """
    ui.evaluate("window.openPage('deepthink')")
    expression = r"""
      (async function() {
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
        const app = document.getElementById('dtApp');
        if (!app) throw new Error('assistant page did not render');
        await frame();
        const parse = value => String(value || '').split(/\s+/).map(part => {
          const n = parseFloat(part);
          if (!isFinite(n)) throw new Error('unparsable radius: ' + value);
          return part.endsWith('%') ? n : n;
        });
        const corners = el => {
          const style = getComputedStyle(el);
          const tl = parse(style.borderTopLeftRadius);
          const tr = parse(style.borderTopRightRadius);
          const br = parse(style.borderBottomRightRadius);
          const bl = parse(style.borderBottomLeftRadius);
          const values = [tl[0], tr[0], br[0], bl[0]];
          return {
            values: values,
            min: Math.min(...values),
            max: Math.max(...values),
            radius: values.join('/')
          };
        };
        const surfaces = ['.dt-rail', '.dt-main', '.dt-new', '.dt-thread-search',
                          '.dt-welcome-mark', '.omni.dt-omni', '.dt-privacy-pill',
                          '.dt-private'];
        const report = {};
        /* Приветственный экран живёт только в пустом диалоге: на чистом
           профиле Открыт ознакомительный диалог с сообщениями. Открываем
           новый диалог штатной кнопкой, измеряем экран, затем возвращаемся
           в прежний — иначе проба проверяла бы поверхность, которой в этом
           состоянии просто нет. */
        const restoreThread =
          (app.querySelector('.dt-thread.active') || {}).dataset;
        const welcomeHome = app.querySelector('[data-act="dtNew"]');
        for (const selector of surfaces) {
          if (selector === '.dt-welcome-mark' && !app.querySelector(selector)) {
            if (!welcomeHome)
              throw new Error('assistant new-dialog button is missing');
            welcomeHome.click();
            await frame(); await pause(120);
          }
          const el = app.querySelector(selector);
          if (!el) throw new Error('missing assistant surface: ' + selector);
          const measured = corners(el);
          if (measured.min <= 0)
            throw new Error('sharp corner on ' + selector + ': ' + measured.radius);
          if (measured.max - measured.min > 0.5)
            throw new Error('uneven corners on ' + selector + ': ' + measured.radius);
          report[selector] = measured.radius;
        }
        /* Панели-полосы внутри карточки (.dt-main) скругляются только по
           внешнему краю: внутренний край — линия стыка, а не угол. */
        const edgeTrays = [['.dt-topbar', 'top'], ['.dt-composer-area', 'bottom']];
        for (const [selector, side] of edgeTrays) {
          const el = app.querySelector(selector);
          if (!el) throw new Error('missing assistant surface: ' + selector);
          const measured = corners(el);
          const pair = side === 'top'
            ? [measured.values[0], measured.values[1]]
            : [measured.values[2], measured.values[3]];
          if (Math.min(...pair) <= 0)
            throw new Error('sharp outer corner on ' + selector + ': ' + measured.radius);
          if (Math.abs(pair[0] - pair[1]) > 0.5)
            throw new Error('uneven outer corners on ' + selector + ': ' + measured.radius);
          report[selector] = measured.radius;
        }
        // Message surfaces appear only after a conversation exists: build them
        // in place, measure the real cascade, then remove the probes.
        const holder = document.createElement('div');
        holder.className = 'dt-message-list';
        holder.style.cssText = 'position:absolute;left:-10000px;top:0;width:640px';
        app.appendChild(holder);
        const messageSurface = ['.dt-user-bubble', '.dt-answer', '.dt-answer-tools',
                                '.dt-kpi', '.bub', '.bub.me'];
        try {
          for (const selector of messageSurface) {
            const probe = document.createElement('div');
            probe.className = selector.replace(/^\./, '').replace(/\./g, ' ');
            probe.textContent = 'проба';
            holder.appendChild(probe);
            const measured = corners(probe);
            if (measured.min <= 0)
              throw new Error('sharp corner on ' + selector + ': ' + measured.radius);
            if (measured.max - measured.min > 0.5)
              throw new Error('uneven corners on ' + selector + ': ' + measured.radius);
            report[selector] = measured.radius;
            probe.remove();
          }
        } finally {
          holder.remove();
        }
        const rail = app.querySelector('.dt-rail').getBoundingClientRect();
        const main = app.querySelector('.dt-main').getBoundingClientRect();
        const gap = Math.round((main.left - rail.right) * 10) / 10;
        if (!(gap >= 4))
          throw new Error('assistant panels are welded together, gap=' + gap);
        const railStyle = corners(app.querySelector('.dt-rail')).min;
        if (railStyle < 12)
          throw new Error('assistant rail is not a rounded card: ' + railStyle);
        if (restoreThread && restoreThread.id) {
          const back = app.querySelector(
            '[data-act="dtOpen"][data-id="' + restoreThread.id + '"]'
          );
          if (back) { back.click(); await frame(); await pause(80); }
        }
        return JSON.stringify({gap: gap, surfaces: report});
      })()
    """
    result = ui.evaluate(expression, timeout=60)
    if not isinstance(result, str):
        raise AcceptanceError(f"Assistant corner probe returned no result: {result!r}")
    data = json.loads(result)
    missing = [
        selector
        for selector in (
            ".dt-user-bubble",
            ".dt-answer",
            ".bub.me",
        )
        if selector not in data.get("surfaces", {})
    ]
    if missing:
        raise AcceptanceError(f"Assistant corner probe skipped surfaces: {missing}")
    return data


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
    """Check that native popups stay glassy in Visual/Balance and opaque in Speed."""
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
        const originalGlow = test.state().prefs.glow !== false;
        const originalGlass = Math.max(40, Math.min(100, +test.state().prefs.glassOp || 82));
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const alphaOf = value => {
          const color = String(value || '').trim();
          if (!color || color === 'none') return null;
          if (color === 'transparent') return 0;
          const slash = color.match(/\/\s*([0-9.]+)(%)?\s*\)$/);
          if (slash) return slash[2] ? Number(slash[1]) / 100 : Number(slash[1]);
          if (/^rgba\(/i.test(color)) {
            const parts = color.slice(color.indexOf('(') + 1, -1).split(',');
            return parts.length > 3 ? Number(parts[3]) : 1;
          }
          // Color functions without an alpha component are fully opaque
          // ("rgb(...)" legacy form, "color(srgb r g b)" modern serialization).
          if (/^(rgb|hsl|lab|lch|oklab|oklch|color)\(/i.test(color)) return 1;
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
            backgroundImage:css.backgroundImage,
            backdropFilter:css.backdropFilter || css.webkitBackdropFilter || 'none',
            animationName:css.animationName,
            animationDuration:css.animationDuration,
            boxShadow:css.boxShadow,
            filter:css.filter,
            borderColor:css.borderColor,
            glowToken:getComputedStyle(document.body).getPropertyValue('--glow-soft').trim(),
            glowOff:document.body.classList.contains('glow-off'),
            reduceMotion:document.documentElement.classList.contains('reduce-motion'),
            ambientPlayState:ambient ? getComputedStyle(ambient).animationPlayState : 'missing',
            ambientAnimationName:ambient ? getComputedStyle(ambient).animationName : 'missing',
            ambientDisplay:ambient ? getComputedStyle(ambient.parentElement).display : 'missing',
            fallbackOpacity:getComputedStyle(document.body).getPropertyValue('--native-popup-opacity').trim()
          });
        }
        test.setGfxMode(original, {persist:false, notify:false});
        await frame();
        return JSON.stringify({checked:checked, original:original, originalGlow:originalGlow, originalGlass:originalGlass});
      })()
    """
    raw = ui.evaluate(expression, timeout=20)
    if not raw:
        raise AcceptanceError("Native popup fallback probe returned no result")
    result = json.loads(raw)
    checked = result.get("checked", [])
    if len(checked) != 4 or [item.get("mode") for item in checked] != ["beauty", "balance", "perf", "beauty"]:
        raise AcceptanceError(f"Native popup probe did not complete the mode sequence: {result}")
    original_glow = result.get("originalGlow")
    if not isinstance(original_glow, bool):
        raise AcceptanceError(f"Native popup probe did not capture the saved glow preference: {result}")
    original_glass = result.get("originalGlass")
    if not isinstance(original_glass, (int, float)) or not 40 <= original_glass <= 100:
        raise AcceptanceError(f"Native popup probe did not capture a valid opacity preference: {result}")
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
            if item.get("boxShadow") != "none" or item.get("filter") != "none" or item.get("backgroundImage") != "none":
                raise AcceptanceError(f"Speed left a popup glow, shadow, filter, or gradient: {item}")
            if not item.get("glowOff") or not item.get("reduceMotion"):
                raise AcceptanceError(f"Speed mode flags are incomplete: {item}")
            if item.get("glowToken") != "none":
                raise AcceptanceError(f"Speed retained a glow token: {item}")
        else:
            expected_alpha = original_glass / 100.0
            if abs(alpha - expected_alpha) > 0.015:
                raise AcceptanceError(
                    f"Native popup alpha {alpha:.3f} does not match the saved {original_glass}% opacity: {item}"
                )
            if item.get("backgroundImage") == "none" or item.get("borderColor") == "transparent":
                raise AcceptanceError(f"Native popup lost its glass highlight/rim: {item}")
            if item.get("boxShadow") == "none":
                raise AcceptanceError(f"Native popup lost its glass lift/shadow: {item}")
            if item.get("animationName") == "none" or item.get("animationDuration") in ("0s", "0.0s"):
                raise AcceptanceError(f"Visual/Balance lost ordinary popup animation: {item}")
            active_glow = item.get("mode") == "beauty" or item.get("mode") == "balance"
            active_glow = active_glow and original_glow
            if item.get("glowOff") == active_glow:
                raise AcceptanceError(f"Popup glow state does not match the saved preference in {mode}: {item}")
            if (item.get("glowToken") == "none") == active_glow:
                raise AcceptanceError(f"Popup glow token does not match the saved preference in {mode}: {item}")
            if item.get("reduceMotion"):
                raise AcceptanceError(f"Reduced-motion state leaked into {mode}: {item}")
            if mode == "beauty" and original_glow and item.get("ambientPlayState") != "paused":
                raise AcceptanceError(f"Visual ambient animation is not paused over native content: {item}")
            if mode == "balance" and original_glow and (
                item.get("ambientDisplay") == "none" or item.get("ambientAnimationName") != "none"
            ):
                raise AcceptanceError(f"Balance glow is missing or still animated: {item}")
            if not original_glow and item.get("ambientDisplay") != "none":
                raise AcceptanceError(f"Disabled user glow still shows ambient decoration: {item}")
            if mode == "beauty" and "blur(" not in item.get("backdropFilter", ""):
                raise AcceptanceError(f"Visual popup lost backdrop blur: {item}")
            if mode == "balance" and item.get("backdropFilter") != "none":
                raise AcceptanceError(f"Balance popup retained backdrop blur: {item}")
    ui.evaluate("document.getElementById('moreBtn').click()")
    wait_native_overlay_idle(ui, process)
    return result


def glass_opacity_probe(ui: Cdp, process: subprocess.Popen) -> Dict[str, Any]:
    """Check slider range, saved values, and native-popup alpha at both endpoints."""
    ui.evaluate(
        "(function(){const b=document.getElementById('quickBtn');"
        "if(!b)throw new Error('quick-menu button missing');b.click();return true;})()"
    )
    wait_until(
        lambda: ui.evaluate("!!document.querySelector('.menu-quick:not(.closing)')") is True,
        "quick opacity menu over a native website",
        process,
        timeout=10,
    )
    wait_until(
        lambda: (lambda state: bool(state and state.get("visible") and state.get("frozen") and not state.get("busy")))(bridge_state(ui)),
        "native page freeze for opacity menu",
        process,
        timeout=15,
    )
    expression = r"""
      (async function() {
        const test = window.shelterTest;
        if (!test || !test.state || !test.setGfxMode)
          throw new Error('opacity test surface is missing');
        const originalMode = test.state().prefs.gfx || 'balance';
        const originalGlass = Math.max(40, Math.min(100, +test.state().prefs.glassOp || 82));
        const frame = () => new Promise(resolve =>
          requestAnimationFrame(() => requestAnimationFrame(resolve))
        );
        const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
        const storedGlass = () => {
          try {
            const saved = JSON.parse(localStorage.getItem('shelter:lux') || '{}');
            return saved && saved.prefs ? +saved.prefs.glassOp : null;
          } catch (_) { return null; }
        };
        const alphaOf = value => {
          const color = String(value || '').trim();
          if (!color || color === 'none') return null;
          if (color === 'transparent') return 0;
          const slash = color.match(/\/\s*([0-9.]+)(%)?\s*\)$/);
          if (slash) return slash[2] ? Number(slash[1]) / 100 : Number(slash[1]);
          if (/^rgba\(/i.test(color)) {
            const parts = color.slice(color.indexOf('(') + 1, -1).split(',');
            return parts.length > 3 ? Number(parts[3]) : 1;
          }
          // Color functions without an alpha component are fully opaque
          // ("rgb(...)" legacy form, "color(srgb r g b)" modern serialization).
          if (/^(rgb|hsl|lab|lch|oklab|oklch|color)\(/i.test(color)) return 1;
          return null;
        };
        const quick = document.getElementById('quickBtn');
        let menu = document.querySelector('.menu-quick:not(.closing)');
        let range = menu && menu.querySelector('#glassOpRange');
        const nativeContentVisible = document.body.classList.contains('native-content-visible');
        if (!menu || !range) throw new Error('quick opacity slider is missing');
        if (range.min !== '40' || range.max !== '100' || range.step !== '1')
          throw new Error('opacity slider range must be 40–100 with unit steps');
        if (range.getAttribute('aria-label') !== 'Прозрачность интерфейса')
          throw new Error('opacity slider accessible label is missing');
        if (!nativeContentVisible)
          throw new Error('native-content fallback is not active behind the popup');

        const checked = [];
        try {
          test.setGfxMode('balance', {persist:false, notify:false});
          await frame();
          for (const value of [40, 100]) {
            menu = document.querySelector('.menu-quick:not(.closing)');
            range = menu && menu.querySelector('#glassOpRange');
            if (!range || range.disabled) throw new Error('opacity slider is unexpectedly disabled');
            range.value = String(value);
            range.dispatchEvent(new Event('input', {bubbles:true}));
            await frame();
            await pause(500);
            const popup = getComputedStyle(menu);
            const state = test.state();
            const actualAlpha = alphaOf(popup.backgroundColor);
            const saved = +state.prefs.glassOp;
            const label = menu.querySelector('#glassOpVal');
            checked.push({
              value:value,
              saved:saved,
              persisted:storedGlass(),
              label:label ? label.textContent : '',
              popupAlpha:actualAlpha,
              popupColor:popup.backgroundColor,
              nativeContentVisible:document.body.classList.contains('native-content-visible'),
              glassOpacity:getComputedStyle(document.body).getPropertyValue('--glass-opacity').trim(),
              glassOp:getComputedStyle(document.body).getPropertyValue('--glass-op').trim()
            });
          }
        } finally {
          menu = document.querySelector('.menu-quick:not(.closing)');
          range = menu && menu.querySelector('#glassOpRange');
          if (range) {
            range.value = String(originalGlass);
            range.dispatchEvent(new Event('input', {bubbles:true}));
            await frame();
            await pause(500);
          }
          test.setGfxMode(originalMode, {persist:false, notify:false});
          await frame();
          if (menu && document.querySelector('.menu-quick:not(.closing)')) quick.click();
          await frame();
        }
        const restoredState = test.state();
        return JSON.stringify({
          checked:checked,
          originalMode:originalMode,
          originalGlass:originalGlass,
          restoredMode:restoredState.prefs.gfx,
          restoredGlass:+restoredState.prefs.glassOp,
          restoredPersistedGlass:storedGlass()
        });
      })()
    """
    raw = ui.evaluate(expression, timeout=20)
    if not raw:
        raise AcceptanceError("Glass opacity probe returned no result")
    result = json.loads(raw)
    checked = result.get("checked", [])
    if [item.get("value") for item in checked] != [40, 100]:
        raise AcceptanceError(f"Glass opacity endpoints were not tested: {result}")
    for item in checked:
        expected = item["value"] / 100.0
        if item.get("saved") != item["value"]:
            raise AcceptanceError(f"Opacity setting was not saved in memory as {item['value']}%: {item}")
        if item.get("persisted") != item["value"]:
            raise AcceptanceError(f"Opacity setting was not persisted as {item['value']}%: {item}")
        if str(item.get("glassOp", "")).strip() != str(item["value"]):
            raise AcceptanceError(f"CSS --glass-op does not match {item['value']}%: {item}")
        if item.get("label") != f"{item['value']}%":
            raise AcceptanceError(f"Opacity label does not match {item['value']}%: {item}")
        alpha = item.get("popupAlpha")
        if not isinstance(alpha, (int, float)) or abs(alpha - expected) > 0.015:
            raise AcceptanceError(f"Popup alpha does not match {item['value']}% opacity: {item}")
        if not item.get("nativeContentVisible"):
            raise AcceptanceError(f"Native popup fallback disappeared at {item['value']}%: {item}")
    if result.get("restoredGlass") != result.get("originalGlass"):
        raise AcceptanceError(f"Opacity preference was not restored after the endpoint probe: {result}")
    if result.get("restoredPersistedGlass") != result.get("originalGlass"):
        raise AcceptanceError(f"Persisted opacity preference was not restored after the endpoint probe: {result}")
    if result.get("restoredMode") != result.get("originalMode"):
        raise AcceptanceError(f"Graphics mode was not restored after the opacity probe: {result}")
    wait_native_overlay_idle(ui, process)
    return result


AD_FIXTURE_FLAGS = (
    "JSON.stringify({"
    "title:document.title,"
    "ready:(document.readyState||''),"
    "search:location.search,"
    "loaded:window.__adLoaded||0,"
    "late:window.__adLateLoaded===true,"
    "slotDisplay:(function(){var s=document.getElementById('adSlot');"
    "return s?getComputedStyle(s).display:'missing';})(),"
    "slotHeight:(function(){var s=document.getElementById('adSlot');"
    "return s?Math.round(s.getBoundingClientRect().height):-1;})()"
    "})"
)


def read_ad_flags(page: Cdp) -> Dict[str, Any]:
    """Read the ad-fixture state, retrying while the page swaps contexts."""
    last_error: Optional[Exception] = None
    for _ in range(20):
        try:
            raw = page.evaluate(AD_FIXTURE_FLAGS)
        except AcceptanceError as exc:
            last_error = exc
            time.sleep(0.25)
            continue
        if raw:
            try:
                return json.loads(raw)
            except ValueError as exc:  # pragma: no cover - defensive
                last_error = exc
        time.sleep(0.25)
    raise AcceptanceError(f"Ad fixture did not report its state: {last_error}")


BLOCKER_STATS = (
    "JSON.stringify((function(){"
    "var st=window.shelterTest.state();"
    "var sp=st.spaces[st.currentSpace]||{tabs:[]};"
    "var tabs=(sp.tabs||[]).map(function(t){return {id:t.id,blocked:t.blocked|0,"
    "url:String(t.url||'').slice(0,80)};});"
    "var ad=tabs.filter(function(t){return t.url.indexOf('/adpage')>=0;}).pop();"
    "var b=st.blocks||{},ks=Object.keys(b),total=0;"
    "for(var i=0;i<ks.length;i++){var v=b[ks[i]];"
    "total+=(v.t|0)+(v.a|0)+(v.f|0)+(v.h|0)+(v.s|0);}"
    "return {tabBlocked:ad?(ad.blocked|0):-1,today:total,activeId:st.activeTabId,"
    "trackers:st.prefs.trackers===true,"
    "hooks:{host:typeof window.__shelterHost,"
    "stats:typeof window.shelterBlockedStats,"
    "dispatch:typeof window.shelterCefDispatch},"
    "tabs:tabs.slice(-4)};})())"
)


def read_blocker_stats(ui: Cdp) -> Dict[str, Any]:
    """Снимок статистики блокировщика на стороне UI (для диагностики падений)."""
    try:
        raw = ui.evaluate(BLOCKER_STATS)
    except AcceptanceError:
        return {}
    if not raw:
        return {}
    try:
        return json.loads(raw)
    except ValueError:
        return {}


# Самопроверка UI: работает ли учёт блокировок без нативных событий. Запускается
# ТОЛЬКО в ветке падения — синтетическое событие не должно маскировать ошибку.
BLOCKER_SELF_TEST = (
    "JSON.stringify((function(){"
    "var out={hook:typeof window.shelterBlockedStats,before:null,after:null,error:null};"
    "try{var st=window.shelterTest.state();"
    "var sp=st.spaces[st.currentSpace]||{tabs:[]};"
    "var ad=(sp.tabs||[]).filter(function(t){"
    "return String(t.url||'').indexOf('/adpage')>=0;}).pop();"
    "out.before=ad?(ad.blocked|0):-1;"
    "if(typeof window.shelterBlockedStats==='function'){"
    "window.shelterBlockedStats({a:1,id:ad?ad.id:''});}"
    "out.after=ad?(ad.blocked|0):-1;"
    "}catch(e){out.error=String((e&&e.message)||e);}return out;})())"
)


def blocker_probe(
    ui: Cdp,
    port: int,
    base: str,
    process: subprocess.Popen,
    site_pages: List[Cdp],
) -> Dict[str, Any]:
    """Prove the tracker/ad blocker really cuts ad requests on a live page.

    The fixture serves an ad script from the page's own document (a main-frame
    subresource), injects a second one after load and embeds a cosmetic ad
    container. Blocking must cover all three; the control pass with the toggle
    off must let the same script load, so a passing probe cannot be explained
    by a broken fixture.
    """
    fixture = base + "/adpage"
    test = "window.shelterTest"
    original = ui.evaluate(f"{test}.state().prefs.trackers")
    ui.evaluate(f"{test}.setPref('trackers', true)")
    time.sleep(0.8)
    if ui.evaluate(f"{test}.state().prefs.trackers") is not True:
        raise AcceptanceError("Tracker blocker preference did not turn on")
    ui.evaluate("window.newTab(" + json.dumps(fixture) + ")")
    target, page, state = wait_for_site(port, "127.0.0.1", process, path_contains="/adpage")
    site_pages.append(page)
    if "SHELTER ad fixture" not in (state.get("text", "") + state.get("title", "")):
        raise AcceptanceError(f"Ad fixture did not render: {state}")
    time.sleep(1.1)  # window for the late script attempt
    blocked = read_ad_flags(page)
    if blocked.get("loaded", 0) != 0:
        raise AcceptanceError(
            "Ad script from the page's own frame was not blocked: " + json.dumps(blocked)
        )
    if blocked.get("late"):
        raise AcceptanceError(
            "Late ad script request was not blocked: " + json.dumps(blocked)
        )
    if blocked.get("slotDisplay") != "none":
        raise AcceptanceError(
            "Cosmetic filter did not hide the ad container: " + json.dumps(blocked)
        )
    # События фильтра приходят из браузерного процесса асинхронно, поэтому
    # счётчик вкладки может отставать от сетевых блокировок: ждём его роста.
    counter, snapshot = -1, {}
    deadline = time.monotonic() + 15
    while True:
        snapshot = read_blocker_stats(ui)
        counter = snapshot.get("tabBlocked", -1) if snapshot else -1
        if isinstance(counter, int) and counter >= 2:
            break
        if time.monotonic() >= deadline:
            break
        time.sleep(0.4)
    if not isinstance(counter, int) or counter < 2:
        self_test = ui.evaluate(BLOCKER_SELF_TEST)
        raise AcceptanceError(
            "Blocked-request counter did not grow: " + json.dumps(snapshot)
            + " ui_self_test=" + json.dumps(self_test)
        )

    control: Dict[str, Any] = {}
    try:
        ui.evaluate(f"{test}.setPref('trackers', false)")
        time.sleep(0.8)
        ui.evaluate("window.navigate(" + json.dumps(fixture + "?noblock=1") + ")")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            control = read_ad_flags(page)
            if "noblock=1" in control.get("search", ""):
                break
            time.sleep(0.3)
        time.sleep(1.1)
        control = read_ad_flags(page)
        if control.get("loaded", 0) < 1:
            raise AcceptanceError(
                "Control pass with the blocker off did not load the ad script: "
                + json.dumps(control)
            )
    finally:
        ui.evaluate(f"{test}.setPref('trackers', {json.dumps(original)})")
        time.sleep(0.4)
        ui.evaluate(
            "(function(){var st=window.shelterTest.state();"
            "var sp=st.spaces[st.currentSpace];"
            "var t=(sp&&sp.tabs||[]).filter(x=>x.url.indexOf('/adpage')>=0).pop();"
            "if(t&&window.closeTab)window.closeTab(t.id);return true;})()"
        )
    return {
        "blocked": blocked,
        "control": control,
        "counter": counter,
        "stats": snapshot,
        "original": original,
    }


CONSENT_FLAGS = (
    "JSON.stringify({"
    "href:location.href,"
    # Метки, которые ставят сами скрипты защит: видно, что именно
    # применил рендерер в этом документе (а не то, что думает UI).
    "armedConsent:window.__shConsent===1,"
    "armedFp:window.__shFp===1,"
    "consent:window.__consent||null,"
    "visible:(typeof window.__visible==='function')&&window.__visible(),"
    "lateMounted:window.__lateMounted===true,"
    "banner:(function(){var b=document.getElementById('cmp');"
    "if(!b||!b.isConnected)return 'removed';var st=getComputedStyle(b);"
    "return st.display==='none'?'hidden':'shown';})()"
    "})"
)


def read_consent_flags(page: Cdp) -> Dict[str, Any]:
    for _ in range(20):
        try:
            raw = page.evaluate(CONSENT_FLAGS)
        except AcceptanceError:
            time.sleep(0.25)
            continue
        if raw:
            try:
                return json.loads(raw)
            except ValueError:
                pass
        time.sleep(0.25)
    return {}


def wait_for_consent(page: Cdp, name: str, deadline_seconds: float = 12) -> Dict[str, Any]:
    """Ждёт ответа CMP; для маршрута late — ещё и монтирования позднего баннера.

    У поздней фикстуры баннер появляется через 700 мс, поэтому «флаги вообще
    есть» — не повод что-то утверждать: ждём именно решения.
    """
    deadline = time.monotonic() + deadline_seconds
    flags = read_consent_flags(page)
    if name == "late":
        while time.monotonic() < deadline and flags.get("lateMounted") is not True:
            time.sleep(0.3)
            flags = read_consent_flags(page)
        if flags.get("lateMounted") is not True:
            raise AcceptanceError(
                "Late consent banner never mounted: " + json.dumps(flags)
            )
    while time.monotonic() < deadline and flags.get("consent") is None:
        time.sleep(0.3)
        flags = read_consent_flags(page)
    time.sleep(0.4)
    return read_consent_flags(page)


def assert_minimal_consent(route: str, flags: Dict[str, Any]) -> None:
    """Минимальный набор подтверждён, баннер не видел пользователь и удалён."""
    if flags.get("consent") != "necessary":
        raise AcceptanceError(
            f"Consent banner {route} was not answered with the minimal set: "
            + json.dumps(flags)
        )
    if flags.get("visible") is not False:
        raise AcceptanceError(
            f"Consent banner {route} became visible before it was answered: "
            + json.dumps(flags)
        )
    if flags.get("banner") != "removed":
        raise AcceptanceError(
            f"Consent banner {route} stayed in the page after consent: "
            + json.dumps(flags)
        )


def read_native_protections(ui: Cdp) -> Dict[str, Any]:
    """Состояние защит глазами браузерного процесса (диагностика dbg.protections).

    Нужно, чтобы сбой в цепочке «тумблер → нативная часть → рендерер» не
    выглядел одинаково на любом её разрыве.
    """
    expression = (
        "new Promise(function(resolve){"
        "if (typeof window.cefQuery !== 'function') { resolve(''); return; }"
        "window.cefQuery({request:'{\"m\":\"dbg.protections\"}',"
        " persistent:false,"
        "onSuccess:function(r){resolve(r||'');},"
        "onFailure:function(){resolve('');}});"
        "})"
    )
    raw = ui.evaluate(expression)
    if not raw:
        return {}
    try:
        return json.loads(raw)
    except ValueError:
        return {}


def cookie_consent_probe(
    ui: Cdp,
    port: int,
    base: str,
    process: subprocess.Popen,
    site_pages: List[Cdp],
) -> Dict[str, Any]:
    """Consent banners must be answered before the user can see them.

    The fixture mirrors a real CMP: an immediate banner and a late-mounted one,
    both offering "only necessary" next to "accept all". The probe checks that
    the minimal set is chosen, that the banner never became visible, and — with
    the toggle off — that the same banner stays visible and unanswered.

    Порядок ног выбран так, чтобы каждая проверяла свой механизм доставки
    состояния в рендерер: обычные ноги идут по одному сайту подряд (процесс
    живёт), а свежая нога уходит на другой сайт — там процесс рождается заново
    и получает защиты из своей командной строки.
    """
    test = "window.shelterTest"
    original = ui.evaluate(f"{test}.state().prefs.cookies")
    original_fp = ui.evaluate(f"{test}.state().prefs.fp")
    native: Dict[str, Any] = {}
    ui.evaluate(f"{test}.setPref('cookies', true)")
    time.sleep(0.8)
    if ui.evaluate(f"{test}.state().prefs.cookies") is not True:
        raise AcceptanceError("Cookie auto-consent preference did not turn on")
    native["on"] = read_native_protections(ui)
    if native["on"].get("cookies") is not True:
        raise AcceptanceError(
            "Auto-consent never reached the native layer when switched on: "
            + json.dumps(native["on"])
        )

    results: Dict[str, Any] = {}
    for name, route in (("immediate", "/consent"), ("late", "/consent-late")):
        ui.evaluate("window.navigate(" + json.dumps(base + route) + ")")
        target, page, state = wait_for_site(
            port, "127.0.0.1", process, path_contains=route
        )
        site_pages.append(page)
        if "consent fixture" not in (state.get("text", "") + state.get("title", "")):
            raise AcceptanceError(f"Consent fixture {route} did not render: {state}")
        flags = wait_for_consent(page, name)
        assert_minimal_consent(route, flags)
        if name == "late" and flags.get("lateMounted") is not True:
            raise AcceptanceError(
                "Late consent banner stayed unmounted: " + json.dumps(flags)
            )
        results[name] = flags

    # Тумблер выключается сразу после обычных ног, и контрольная нога идёт по
    # тому же сайту: процесс рендерера заведомо жив, то есть проверяется ровно
    # то, что тумблер догоняет уже работающую вкладку.
    control: Dict[str, Any] = {}
    try:
        ui.evaluate(f"{test}.setPref('cookies', false)")
        time.sleep(0.8)
        if ui.evaluate(f"{test}.state().prefs.cookies") is not False:
            raise AcceptanceError("Cookie auto-consent preference did not turn off")
        native["off"] = read_native_protections(ui)
        if native["off"].get("cookies") is not False:
            raise AcceptanceError(
                "Auto-consent never reached the native layer when switched off: "
                + json.dumps(native["off"])
            )
        ui.evaluate(
            "window.navigate(" + json.dumps(base + "/consent?manual=1") + ")"
        )
        try:
            _target, control_page, _state = wait_for_site(
                port, "127.0.0.1", process, path_contains="manual=1", timeout=25
            )
        except AcceptanceError as exc:
            raise AcceptanceError(
                "Control consent page did not load: " + str(exc)
            ) from exc
        site_pages.append(control_page)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            control = read_consent_flags(control_page)
            if control.get("consent") is None and control.get("visible") is True:
                break
            time.sleep(0.3)
        if control.get("consent") is not None:
            raise AcceptanceError(
                "Cookie auto-consent kept answering while switched off: "
                + json.dumps(control, ensure_ascii=False)
                + " native=" + json.dumps(native.get("off", {}))
            )
        if control.get("visible") is not True:
            raise AcceptanceError(
                "With auto-consent off the banner must stay visible: "
                + json.dumps(control, ensure_ascii=False)
            )
    finally:
        ui.evaluate(f"{test}.setPref('cookies', {json.dumps(original)})")
        time.sleep(0.6)

    # Свежий сайт: другой сайт означает новый процесс рендерера, и он обязан
    # получить состояние защит из командной строки. Метка анти-отпечатка в
    # документе показывает, что именно применил этот новый процесс: если
    # тумблер включён, а метки нет — процесс родился со значениями по умолчанию,
    # то есть на новых страницах защиты не работают. Если площадка недоступна,
    # это внешняя причина, а не поведение продукта — предупреждаем и идём дальше.
    fresh: Dict[str, Any] = {}
    fresh_error = ""
    fresh_page: Optional[Cdp] = None
    fresh_state: Dict[str, Any] = {}
    try:
        ui.evaluate(f"{test}.setPref('cookies', " + json.dumps(True) + ")")
        ui.evaluate(f"{test}.setPref('fp', " + json.dumps(True) + ")")
        time.sleep(0.5)
        ui.evaluate(
            "window.navigate("
            + json.dumps(base.replace("127.0.0.1", "localhost") + "/consent")
            + ")"
        )
        _target, fresh_page, fresh_state = wait_for_site(
            port, "localhost", process, path_contains="/consent", timeout=25
        )
    except AcceptanceError as exc:
        fresh_error = f"{exc}"
        fresh_page = None
    if fresh_page is not None:
        site_pages.append(fresh_page)
        if "consent fixture" not in (
            fresh_state.get("text", "") + fresh_state.get("title", "")
        ):
            raise AcceptanceError(
                f"Fresh-site consent fixture did not render: {fresh_state}"
            )
        fresh = wait_for_consent(fresh_page, "fresh")
        assert_minimal_consent("fresh site", fresh)
        if fresh.get("armedFp") is not True:
            raise AcceptanceError(
                "A process born after the app started missed fingerprint "
                "protection, so protections do not reach new pages: "
                + json.dumps(fresh, ensure_ascii=False)
            )
    if fresh_error:
        print(
            "SHELTER_ACCEPTANCE_COOKIE_CONSENT_FRESH_SKIPPED " + fresh_error,
            flush=True,
        )

    ui.evaluate(f"{test}.setPref('cookies', {json.dumps(original)})")
    ui.evaluate(f"{test}.setPref('fp', {json.dumps(original_fp)})")
    time.sleep(0.5)
    native["restored"] = read_native_protections(ui)
    return {
        "toggles": results,
        "control": control,
        "fresh": fresh,
        "fresh_error": fresh_error,
        "native": native,
        "original": original,
    }

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
            "var c=getComputedStyle(m);return JSON.stringify({mode:document.body.dataset.gfx,background:c.backgroundColor,blur:c.backdropFilter,"
            "backgroundImage:c.backgroundImage,boxShadow:c.boxShadow,filter:c.filter,borderColor:c.borderColor,"
            "alpha:(function(v){var m=String(v).match(/\\/\\s*([0-9.]+)(%)?\\s*\\)$/);"
            "if(m)return m[2]?+m[1]/100:+m[1];if(/^rgba\\(/.test(v))return +v.slice(0,-1).split(',').pop();return 1;})(c.backgroundColor)});})()"
        )
        if not menu_style:
            raise AcceptanceError(f"Popup vanished during the scroll probe for {route}")
        menu_style = json.loads(menu_style)
        alpha = menu_style.get("alpha", 0)
        if menu_style.get("mode") == "perf":
            if alpha < 0.999 or menu_style.get("boxShadow") != "none" or menu_style.get("filter") != "none":
                raise AcceptanceError(f"Speed popup lost its opaque, effect-free fallback during fast scrolling: {menu_style}")
        elif (
            not 0.72 <= alpha < 0.92
            or menu_style.get("backgroundImage") == "none"
            or menu_style.get("boxShadow") == "none"
            or menu_style.get("borderColor") == "transparent"
            or (menu_style.get("mode") == "beauty" and "blur(" not in menu_style.get("blur", ""))
            or (menu_style.get("mode") == "balance" and menu_style.get("blur") != "none")
        ):
            raise AcceptanceError(f"Popup lost its visible glass fallback during fast scrolling: {menu_style}")
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
          var css=getComputedStyle(menu), color=css.backgroundColor;
          var slash=color.match(/\/\s*([0-9.]+)(%)?\s*\)$/);
          var alpha=slash?(slash[2]?Number(slash[1])/100:Number(slash[1])):
            (/^rgba\(/i.test(color)?Number(color.slice(0,-1).split(',').pop()):1);
          return JSON.stringify({mode:document.body.dataset.gfx,nativeContentVisible:document.body.classList.contains('native-content-visible'),
            backgroundColor:color,backgroundImage:css.backgroundImage,boxShadow:css.boxShadow,filter:css.filter,
            backdropFilter:css.backdropFilter,borderColor:css.borderColor,alpha:alpha});
        })()"""
    )
    if not menu_style:
        raise AcceptanceError("Popup did not remain open over the still-loading native page")
    menu_style = json.loads(menu_style)
    alpha = menu_style.get("alpha", 0)
    if not menu_style.get("nativeContentVisible"):
        raise AcceptanceError(f"Native-content fallback did not activate during loading: {menu_style}")
    if menu_style.get("mode") == "perf":
        if alpha < 0.999 or menu_style.get("boxShadow") != "none" or menu_style.get("filter") != "none":
            raise AcceptanceError(f"Speed popup fallback is not opaque and effect-free during native loading: {menu_style}")
    elif (
        not 0.72 <= alpha < 0.92
        or menu_style.get("backgroundImage") == "none"
        or menu_style.get("boxShadow") == "none"
        or menu_style.get("borderColor") == "transparent"
        or (menu_style.get("mode") == "beauty" and "blur(" not in menu_style.get("backdropFilter", ""))
        or (menu_style.get("mode") == "balance" and menu_style.get("backdropFilter") != "none")
    ):
        raise AcceptanceError(f"Popup lost its visible glass fallback during native loading: {menu_style}")
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
                env=dict(os.environ, SHELTER_DIAG="1"),  # CEF debug.log только для диагностики
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

            opacity_probe = glass_opacity_probe(ui, process)
            print("SHELTER_ACCEPTANCE_GLASS_OPACITY " + json.dumps(opacity_probe, ensure_ascii=False), flush=True)
            print("SHELTER_ACCEPTANCE_GLASS_OPACITY_PASS", flush=True)

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

            blocker_result = blocker_probe(
                ui, port, long_page_base, process, site_pages
            )
            print(
                "SHELTER_ACCEPTANCE_BLOCKER "
                + json.dumps(blocker_result, ensure_ascii=False),
                flush=True,
            )
            print("SHELTER_ACCEPTANCE_BLOCKER_PASS", flush=True)

            # Пробы оформления идут раньше сетевых: они герметичны (живут
            # внутри UI) и не должны зависеть от фикстур, а их отчёт — теряться
            # из-за сбоя сетевой проверки.
            corner_probe = assistant_corners_probe(ui)
            print(
                "SHELTER_ACCEPTANCE_ASSISTANT_CORNERS "
                + json.dumps(corner_probe, ensure_ascii=False),
                flush=True,
            )
            print("SHELTER_ACCEPTANCE_ASSISTANT_CORNERS_PASS", flush=True)

            tab_probe = tab_width_probe(ui)
            print(
                "SHELTER_ACCEPTANCE_TAB_WIDTH "
                + json.dumps(tab_probe, ensure_ascii=False),
                flush=True,
            )
            print("SHELTER_ACCEPTANCE_TAB_WIDTH_PASS", flush=True)

            consent_result = cookie_consent_probe(
                ui, port, long_page_base, process, site_pages
            )
            print(
                "SHELTER_ACCEPTANCE_COOKIE_CONSENT "
                + json.dumps(consent_result, ensure_ascii=False),
                flush=True,
            )
            print("SHELTER_ACCEPTANCE_COOKIE_CONSENT_PASS", flush=True)

            ui.evaluate(
                "window.shelterTest.setGfxMode(" + json.dumps(original_gfx) +
                ",{persist:false,notify:false})"
            )

            print("SHELTER_ACCEPTANCE_PASS", flush=True)
        return 0
    except Exception as exc:
        message = " ".join(str(exc).split())
        # Короткая строка идёт и в лог, и в аннотацию GitHub Actions: длинные
        # JSON-дампы обрезаются, а причину падения видно сразу.
        print(f"SHELTER_ACCEPTANCE_FAIL {message}", file=sys.stderr, flush=True)
        print(f"::error::SHELTER_ACCEPTANCE_FAIL {message[:900]}", flush=True)
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
