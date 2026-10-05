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
import time
import urllib.request
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

try:
    import websocket
except ImportError as exc:  # pragma: no cover - exercised by CI setup
    raise SystemExit("Missing dependency: install websocket-client") from exc


class AcceptanceError(RuntimeError):
    pass


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
        let overflow = 0;
        document.querySelectorAll(
          '.dash-top > *, .dash-right > *, .dash-bottom > *'
        ).forEach(function(card) {
          const rect = card.getBoundingClientRect();
          if (rect.width > 0 && rect.height > 0)
            overflow = Math.max(overflow, rect.right - edge);
        });
        [page, top, right, bottom, document.documentElement].forEach(function(el) {
          if (el) overflow = Math.max(overflow, el.scrollWidth - el.clientWidth);
        });
        return JSON.stringify({
          windowWidth: window.innerWidth,
          viewportWidth: viewport.clientWidth,
          overflowPx: Math.max(0, overflow)
        });
      })()
    """
    raw = ui.evaluate(expression)
    if not raw:
        return {}
    return json.loads(raw)


def page_state(page: Cdp) -> Dict[str, Any]:
    raw = page.evaluate(
        "JSON.stringify({url:location.href,title:document.title,"
        "readyState:document.readyState,"
        "text:(document.body&&document.body.innerText||'').slice(0,800)})"
    )
    return json.loads(raw) if raw else {}


def find_site_page(port: int, host: str) -> Optional[Dict[str, Any]]:
    for target in get_targets(port):
        if (
            target.get("type") == "page"
            and host in target.get("url", "").lower()
            and target.get("webSocketDebuggerUrl")
        ):
            return target
    return None


def wait_for_site(
    port: int,
    host: str,
    process: subprocess.Popen,
    timeout: float = 45,
) -> Tuple[Dict[str, Any], Cdp, Dict[str, Any]]:
    target = wait_until(
        lambda: find_site_page(port, host),
        f"native CEF page for {host}",
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

    try:
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


if __name__ == "__main__":
    raise SystemExit(main())
