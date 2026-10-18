#!/usr/bin/env python3
"""Строгая приёмка геометрии нативной вкладки SHELTER.

Запускает CI-сборку, через локальный CEF DevTools открывает реальный сайт во
вкладке приложения и ждёт маркер «shell: content bounds» в shelter.log.
Проверяет, что область сайта остаётся внутри интерфейса браузера.
"""
from __future__ import annotations

import glob
import json
import os
import platform
import re
import socket
import subprocess
import sys
import time
import urllib.request
from typing import Any, Dict, List, Optional

try:
    import websocket
except ImportError as exc:
    raise SystemExit(
        "Не найден websocket-client: установите его командой "
        "python -m pip install websocket-client"
    ) from exc


class GeometryError(RuntimeError):
    pass


def out(*values: Any) -> None:
    print(*values, flush=True)


def user_data_dir() -> str:
    if platform.system() == "Windows":
        base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA") or "."
        return os.path.join(base, "SHELTER")
    return os.path.expanduser("~/Library/Application Support/SHELTER")


def read_lines(path: str) -> List[str]:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as stream:
            return stream.read().splitlines()
    except OSError:
        return []


def tail_lines(path: str, count: int = 80) -> List[str]:
    return read_lines(path)[-count:]


def dump_diagnostics(logfile: str, stderr_file: str) -> None:
    out("--- shelter.log tail ---")
    for line in tail_lines(logfile):
        out(line)
    out("--- app stderr tail ---")
    for line in tail_lines(stderr_file):
        out(line)
    if platform.system() == "Darwin":
        reports = []
        for directory in (
            os.path.expanduser("~/Library/Logs/DiagnosticReports"),
            "/Library/Logs/DiagnosticReports",
        ):
            reports.extend(glob.glob(os.path.join(directory, "Shelter*")))
        if reports:
            newest = max(reports, key=os.path.getmtime)
            out(f"--- newest crash report: {newest} ---")
            for line in tail_lines(newest, 120):
                out(line)


def kill(process: Optional[subprocess.Popen]) -> None:
    if process is None or process.poll() is not None:
        return
    try:
        if platform.system() == "Windows":
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                capture_output=True,
                check=False,
            )
        else:
            process.terminate()
            try:
                process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=4)
    except Exception as exc:
        out("kill error:", exc)


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
        self.socket.settimeout(timeout)
        self.socket.send(
            json.dumps({"id": call_id, "method": method, "params": params or {}})
        )
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                message = json.loads(self.socket.recv())
            except Exception as exc:
                raise GeometryError(f"CDP {method} receive failed: {exc}") from exc
            if message.get("id") == call_id:
                if "error" in message:
                    raise GeometryError(f"CDP {method} failed: {message['error']}")
                return message.get("result", {})
            if message.get("method"):
                self.events.append(message)
        raise GeometryError(f"CDP {method} timed out after {timeout:g}s")

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
            raise GeometryError(
                "JavaScript evaluation failed: "
                + json.dumps(details, ensure_ascii=False)[:800]
            )
        return response.get("result", {}).get("value")

    def close(self) -> None:
        try:
            self.socket.close()
        except Exception:
            pass


def wait_until(probe, description: str, process: subprocess.Popen, deadline: float):
    last_error: Optional[Exception] = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise GeometryError(
                f"SHELTER exited early with status {process.returncode} "
                f"while waiting for {description}"
            )
        try:
            value = probe()
            if value:
                return value
        except Exception as exc:
            last_error = exc
        time.sleep(min(0.4, max(0.0, deadline - time.monotonic())))
    detail = f"; last error: {last_error}" if last_error else ""
    raise GeometryError(f"Timed out waiting for {description}{detail}")


def main() -> int:
    if len(sys.argv) < 2:
        out("usage: check_geometry.py <exe> [budget_seconds]")
        return 2
    executable = os.path.abspath(sys.argv[1])
    try:
        budget = int(sys.argv[2]) if len(sys.argv) > 2 else 90
    except ValueError:
        out("budget_seconds must be an integer")
        return 2
    if budget <= 0:
        out("budget_seconds must be positive")
        return 2
    if not os.path.isfile(executable):
        out("SHELTER executable not found:", executable)
        return 2

    logfile = os.path.join(user_data_dir(), "shelter.log")
    stderr_file = "geometry-app.log"
    marker = "shell: content bounds"
    seen = len(read_lines(logfile))
    port = reserve_port()
    command = [
        executable,
        f"--remote-debugging-port={port}",
        "--remote-debugging-address=127.0.0.1",
        "--remote-allow-origins=*",
    ]
    env = dict(os.environ)
    env["SHELTER_DIAG"] = "1"
    out("launch:", executable)
    out("DevTools port:", port)
    out("log:", logfile)

    process = None
    cdp = None
    stderr_stream = open(stderr_file, "w", encoding="utf-8", errors="replace")
    deadline = time.monotonic() + budget
    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.DEVNULL,
            stderr=stderr_stream,
            env=env,
        )
        target = wait_until(
            lambda: next(
                (
                    entry
                    for entry in get_targets(port)
                    if entry.get("type") == "page"
                    and entry.get("url", "").startswith("shelter://app/")
                    and entry.get("webSocketDebuggerUrl")
                ),
                None,
            ),
            "the SHELTER UI DevTools target",
            process,
            deadline,
        )
        out("UI target:", target.get("url", ""))
        cdp = Cdp(target["webSocketDebuggerUrl"])
        cdp.call("Runtime.enable")
        cdp.call("Page.enable")
        ready_expression = """(() => {
          const viewport = document.getElementById('viewport');
          if (!viewport || typeof window.getActiveTabId !== 'function' ||
              !window.getActiveTabId() || !window.shelter ||
              typeof window.shelter.navigate !== 'function') return false;
          const rect = viewport.getBoundingClientRect();
          return rect.width > 2 && rect.height > 2;
        })()"""
        wait_until(
            lambda: cdp.evaluate(ready_expression) is True,
            "an initialized UI and visible viewport",
            process,
            deadline,
        )
        viewport = cdp.evaluate(
            "(() => { const r = document.getElementById('viewport').getBoundingClientRect(); "
            "return {x: Math.round(r.x), y: Math.round(r.y), "
            "w: Math.round(r.width), h: Math.round(r.height)}; })()"
        )
        out("UI viewport:", json.dumps(viewport, ensure_ascii=False))
        navigation = cdp.evaluate(
            "(() => { window.shelter.navigate('https://example.com/'); "
            "return {activeTabId: window.getActiveTabId(), started: true}; })()"
        )
        out("native-site navigation:", json.dumps(navigation, ensure_ascii=False))

        line = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise GeometryError(
                    f"SHELTER exited early with status {process.returncode}"
                )
            lines = read_lines(logfile)
            fresh = lines[seen:] if len(lines) >= seen else lines
            matches = [entry.strip() for entry in fresh if marker in entry]
            if matches:
                line = matches[-1]
                break
            time.sleep(min(0.5, max(0.0, deadline - time.monotonic())))
        if not line:
            raise GeometryError(
                f"marker {marker!r} not found in {logfile} within {budget}s"
            )

        out("geometry:", line)
        match = re.search(
            r"x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+) "
            r"client_w=(\d+) client_h=(\d+)",
            line,
        )
        if not match:
            raise GeometryError("cannot parse native geometry line")
        x, y, width, height, client_width, client_height = (
            int(value) for value in match.groups()
        )
        if client_width <= 0 or client_height <= 0:
            raise GeometryError(
                f"bad client size {client_width}x{client_height}"
            )
        if x <= 0 or y <= 0 or width >= client_width or height >= client_height:
            raise GeometryError(
                "content view covers browser chrome "
                f"(x={x} y={y} w={width} h={height} "
                f"client={client_width}x{client_height})"
            )
        out(
            f"OK: content ({x},{y} {width}x{height}) "
            f"inside client {client_width}x{client_height}"
        )
        return 0
    except Exception as exc:
        out(f"FAIL: {exc}")
        try:
            stderr_stream.flush()
        except OSError:
            pass
        dump_diagnostics(logfile, stderr_file)
        return 1
    finally:
        if cdp is not None:
            cdp.close()
        kill(process)
        try:
            stderr_stream.close()
        except OSError:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
