#!/usr/bin/env python3
"""Приёмка геометрии вкладки и native DevTools dock SHELTER.

Запускает CI-сборку, через локальный CEF DevTools открывает реальный сайт во
вкладке приложения и ждёт маркер «shell: content bounds» в shelter.log.
Проверяет, что область сайта остаётся внутри интерфейса браузера, затем
поведенчески открывает CEF DevTools в dock и передаёт Inspect-точку.
"""
from __future__ import annotations

import glob
import json
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.request
from typing import Any, Dict, List, Optional

from ci_lldb_launcher import (
    attach_command,
    attach_is_resuming,
    faulting_thread_summary,
    inferior_pid_from_output,
    inferior_pid_from_process_list,
    wrap_command,
)

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


WINDOW_STATE_MAGIC = "SHELTER_WINDOW_STATE_V2"


def write_window_state(directory: str, mode: str = "maximized") -> str:
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, "window-state-v2")
    with open(path, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(f"{WINDOW_STATE_MAGIC}\n{mode}\n100 100 1440 900\n")
    return path


def read_window_state_mode(path: str) -> str:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as stream:
            lines = stream.read().splitlines()
    except OSError:
        return ""
    if len(lines) < 2 or lines[0].strip() != WINDOW_STATE_MAGIC:
        return ""
    return lines[1].strip()


def read_lines(path: str) -> List[str]:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as stream:
            return stream.read().splitlines()
    except OSError:
        return []


def tail_lines(path: str, count: int = 80) -> List[str]:
    return read_lines(path)[-count:]


def dump_diagnostics(
    logfile: str, stderr_file: str, lldb_trace_file: str = ""
) -> None:
    out("--- shelter.log tail ---")
    for line in tail_lines(logfile):
        out(line)
    out("--- app stderr tail ---")
    for line in tail_lines(stderr_file):
        out(line)
    native_trace_file = lldb_trace_file
    if not native_trace_file and os.environ.get("SHELTER_USE_LLDB") == "1":
        native_trace_file = stderr_file
    if native_trace_file and platform.system() == "Darwin":
        if native_trace_file != stderr_file:
            out("--- LLDB attach output tail ---")
            for line in tail_lines(native_trace_file):
                out(line)
        summary = faulting_thread_summary("\n".join(read_lines(native_trace_file)))
        if summary:
            out("--- LLDB faulting-thread backtrace ---")
            for line in summary:
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


def attach_lldb_to_process(pid: int, lldb_path: str, output_file: str) -> subprocess.Popen:
    """Attach to the ready app immediately before the DevTools call."""
    try:
        command = attach_command(pid, lldb_path=lldb_path)
    except ValueError as exc:
        raise GeometryError(f"native debugger setup failed: {exc}") from exc
    try:
        with open(output_file, "w", encoding="utf-8", errors="replace") as stream:
            debugger = subprocess.Popen(
                command,
                stdin=subprocess.DEVNULL,
                stdout=stream,
                stderr=subprocess.STDOUT,
            )
    except OSError as exc:
        raise GeometryError(f"could not start LLDB attach: {exc}") from exc

    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        transcript = "\n".join(read_lines(output_file))
        if attach_is_resuming(transcript, pid):
            return debugger
        if debugger.poll() is not None:
            detail = " | ".join(read_lines(output_file)[-12:])
            raise GeometryError(
                f"LLDB attach exited with status {debugger.returncode}; {detail}"
            )
        time.sleep(0.1)

    detail = " | ".join(read_lines(output_file)[-12:])
    kill(debugger)
    raise GeometryError(f"LLDB attach did not become ready within 15s; {detail}")


def cleanup_lldb_debuggee(
    process: Optional[subprocess.Popen],
    stderr_file: str,
    executable: str,
    port: int,
    acceptance_succeeded: bool,
) -> None:
    """Stop LLDB's inferior before terminating LLDB so the app cannot linger."""
    if process is None or process.poll() is not None:
        return
    if not acceptance_succeeded:
        try:
            process.wait(timeout=2)
            return
        except subprocess.TimeoutExpired:
            pass

    try:
        with open(stderr_file, "r", encoding="utf-8", errors="replace") as stream:
            output = stream.read()
    except OSError:
        output = ""
    inferior_pid = inferior_pid_from_output(output)
    if inferior_pid is None:
        try:
            process_list = subprocess.run(
                ["ps", "-axo", "pid=,command="],
                capture_output=True,
                text=True,
                timeout=5,
                check=False,
            ).stdout
        except (OSError, subprocess.TimeoutExpired):
            process_list = ""
        inferior_pid = inferior_pid_from_process_list(process_list, executable, port)
    if inferior_pid is None:
        out("LLDB cleanup: inferior PID was not present in debugger output or process list")
        return
    try:
        os.kill(inferior_pid, signal.SIGKILL)
        out(f"LLDB cleanup: terminated inferior PID {inferior_pid}")
    except ProcessLookupError:
        return
    except OSError as exc:
        out(f"LLDB cleanup: could not terminate inferior PID {inferior_pid}: {exc}")
        return
    try:
        process.wait(timeout=8)
    except subprocess.TimeoutExpired:
        kill(process)


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
    native_trace_file = "geometry-lldb-trace.log"
    try:
        state_file = write_window_state(user_data_dir())
    except OSError as exc:
        out("cannot pre-write window state:", exc)
        return 2
    out("pre-written window state:", state_file)
    marker = "shell: content bounds"
    seen = len(read_lines(logfile))
    port = reserve_port()
    command = [
        executable,
        f"--remote-debugging-port={port}",
        "--remote-debugging-address=127.0.0.1",
        "--remote-allow-origins=*",
    ]
    debug_with_lldb = os.environ.get("SHELTER_USE_LLDB") == "1"
    attach_lldb_before_devtools = (
        os.environ.get("SHELTER_ATTACH_LLDB_ON_DEVTOOLS") == "1"
    )
    lldb_path = shutil.which("lldb") or ""
    if (debug_with_lldb or attach_lldb_before_devtools) and platform.system() != "Darwin":
        out("LLDB diagnostics are supported only on macOS")
        return 2
    if debug_with_lldb and attach_lldb_before_devtools:
        out("choose either LLDB launch mode or LLDB attach mode, not both")
        return 2
    try:
        command = wrap_command(
            command,
            enabled=debug_with_lldb,
            platform_name=platform.system(),
            lldb_path=lldb_path,
        )
    except ValueError as exc:
        out("native debugger setup failed:", exc)
        return 2
    if debug_with_lldb:
        out("native crash diagnostics: LLDB thread backtraces enabled")
    if attach_lldb_before_devtools:
        out("native crash diagnostics: LLDB will attach immediately before DevTools")
    env = dict(os.environ)
    env["SHELTER_DIAG"] = "1"
    out("launch:", executable)
    out("DevTools port:", port)
    out("log:", logfile)

    process = None
    native_debugger = None
    cdp = None
    last_ui_state: List[Any] = [None]
    native_open_state: List[Any] = [None]
    native_open_attempts = 0
    native_open_accepted = False
    acceptance_succeeded = False
    stderr_stream = open(stderr_file, "w", encoding="utf-8", errors="replace")
    deadline = time.monotonic() + budget
    try:
        process = subprocess.Popen(
            command,
            stdout=stderr_stream if debug_with_lldb else subprocess.DEVNULL,
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
        out("CDP connected to the SHELTER UI")
        readiness_expression = """(() => {
          const viewport = document.getElementById('viewport');
          const activeTabId = typeof window.getActiveTabId === 'function'
            ? window.getActiveTabId() : null;
          const rect = viewport ? viewport.getBoundingClientRect() : null;
          const hasNavigate = !!window.shelter &&
            typeof window.shelter.navigate === 'function';
          const hasNativeRequest = typeof window.shelterCefRequest === 'function';
          const hasTabInfo = typeof window.__shelterTab === 'function';
          return {
            ready: !!viewport && hasNavigate && hasNativeRequest && hasTabInfo,
            activeTabId: activeTabId,
            hasViewport: !!viewport,
            viewport: rect ? {
              x: Math.round(rect.x), y: Math.round(rect.y),
              w: Math.round(rect.width), h: Math.round(rect.height)
            } : null,
            hasNavigate: hasNavigate,
            hasNativeRequest: hasNativeRequest,
            hasTabInfo: hasTabInfo
          };
        })()"""

        window_state_expression = (
            "new Promise(function(resolve){"
            "if (typeof window.cefQuery !== 'function') { resolve(''); return; }"
            "window.cefQuery({request:'{\"m\":\"dbg.window\"}',"
            " persistent:false,"
            "onSuccess:function(r){resolve(r||'');},"
            "onFailure:function(){resolve('');}});"
            "})"
        )

        def ui_ready() -> bool:
            last_ui_state[0] = cdp.evaluate(readiness_expression)
            return (
                isinstance(last_ui_state[0], dict)
                and last_ui_state[0].get("ready") is True
            )

        try:
            wait_until(
                ui_ready,
                "the initialized UI bridge and active tab",
                process,
                deadline,
            )
        except GeometryError as exc:
            state = json.dumps(last_ui_state[0], ensure_ascii=False)
            raise GeometryError(f"{exc}; last UI state: {state}") from exc
        out("UI bridge ready:", json.dumps(last_ui_state[0], ensure_ascii=False))
        window_state = cdp.evaluate(window_state_expression)
        out("main window state:", window_state)
        try:
            window_info = json.loads(window_state)
        except (TypeError, ValueError):
            window_info = {}
        if window_info.get("maximized") is not True:
            raise GeometryError(
                "main window is not maximized despite saved state 'maximized': "
                f"{window_state!r}"
            )
        if window_info.get("fullscreen") is True:
            raise GeometryError("main window unexpectedly fullscreen")
        navigation = cdp.evaluate(
            "(() => { window.shelter.navigate('https://example.com/'); "
            "return {activeTabId: window.getActiveTabId()}; })()"
        )
        out("native-site navigation requested:", json.dumps(navigation, ensure_ascii=False))

        native_open_expression = """(async () => {
          const id = window.getActiveTabId();
          const viewport = document.getElementById('viewport');
          if (!id || !viewport) return {accepted: false, reason: 'missing UI tab or viewport'};
          const box = viewport.getBoundingClientRect();
          const rect = {
            x: Math.round(box.left), y: Math.round(box.top),
            w: Math.round(box.width), h: Math.round(box.height)
          };
          if (rect.w < 2 || rect.h < 2) {
            return {accepted: false, reason: 'viewport not measurable', id: id, rect: rect};
          }
          const info = window.__shelterTab(id) || {space: 'main', ghost: false};
          const partition = (info.ghost ? 'temp:ghost-' : 'persist:space-') +
            (info.space || 'main');
          const response = await window.shelterCefRequest('view.open', {
            id: id,
            url: 'https://example.com/',
            partition: partition,
            rect: rect,
            visible: true,
            focus: true,
            zoom: 1
          });
          const accepted = !!response && typeof response === 'object' &&
            typeof response.created === 'boolean' &&
            typeof response.navigated === 'boolean';
          return {
            accepted: accepted,
            id: id,
            partition: partition,
            rect: rect,
            response: response
          };
        })()"""
        while time.monotonic() < deadline and not native_open_accepted:
            if process.poll() is not None:
                raise GeometryError(
                    f"SHELTER exited early with status {process.returncode}"
                )
            native_open_attempts += 1
            native_open_state[0] = cdp.evaluate(native_open_expression, timeout=15)
            native_open_accepted = (
                isinstance(native_open_state[0], dict)
                and native_open_state[0].get("accepted") is True
            )
            if native_open_attempts == 1 or native_open_attempts % 10 == 0:
                last_ui_state[0] = cdp.evaluate(readiness_expression)
                out(
                    "native view request:",
                    json.dumps(native_open_state[0], ensure_ascii=False),
                    "UI:",
                    json.dumps(last_ui_state[0], ensure_ascii=False),
                )
            if not native_open_accepted:
                time.sleep(min(0.5, max(0.0, deadline - time.monotonic())))
        if not native_open_accepted:
            raise GeometryError(
                "UI bridge did not confirm view.open after "
                f"{native_open_attempts} attempts; last request: "
                f"{json.dumps(native_open_state[0], ensure_ascii=False)}"
            )
        last_ui_state[0] = cdp.evaluate(readiness_expression)
        out(
            f"native view accepted after {native_open_attempts} attempts:",
            json.dumps(native_open_state[0], ensure_ascii=False),
        )

        geometry_wait = min(45, budget)
        geometry_deadline = time.monotonic() + geometry_wait
        line = None
        while time.monotonic() < geometry_deadline:
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
            time.sleep(min(0.5, max(0.0, geometry_deadline - time.monotonic())))
        if not line:
            raise GeometryError(
                f"marker {marker!r} not found in {logfile} within {geometry_wait}s"
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
        if read_window_state_mode(state_file) != "maximized":
            raise GeometryError("saved window mode was overwritten during startup")
        out("saved window mode intact: maximized")

        # Exercise the real CEF path that the source-only regression tests
        # cannot cover: create the docked Views popup, then request an element
        # inspection against the same already-open DevTools browser.
        devtools_log_start = len(read_lines(logfile))

        def devtools_marker_count(marker: str) -> int:
            lines = read_lines(logfile)
            fresh = (
                lines[devtools_log_start:]
                if len(lines) >= devtools_log_start
                else lines
            )
            return sum(marker in entry for entry in fresh)

        native_trace_line_start = 0
        if attach_lldb_before_devtools:
            if process is None:
                raise GeometryError("cannot attach LLDB before the app process starts")
            native_debugger = attach_lldb_to_process(
                process.pid, lldb_path, native_trace_file
            )
            native_trace_line_start = len(read_lines(native_trace_file))
            out(f"LLDB attached to PID {process.pid} before DevTools request")

        def devtools_or_native_stop():
            if native_debugger is not None:
                trace_lines = read_lines(native_trace_file)
                new_lines = trace_lines[native_trace_line_start:]
                if any(
                    "stop reason" in entry.casefold()
                    or re.search(r"\bframe #0:", entry)
                    for entry in new_lines
                ):
                    return "native-stop"
            return devtools_marker_count("ci: devtools ShowDevTools returned") >= 1

        devtools_deadline = time.monotonic() + min(20, max(5, budget // 3))
        open_result = cdp.evaluate("""(() => {
          const id = typeof window.getActiveTabId === 'function'
            ? window.getActiveTabId() : null;
          if (!id || typeof window.shelterOpenDevtools !== 'function') {
            return {accepted: false, reason: 'DevTools UI entry point unavailable', id: id};
          }
          window.shelterOpenDevtools({tabId: id});
          return {accepted: true, id: id};
        })()""")
        if not isinstance(open_result, dict) or open_result.get("accepted") is not True:
            raise GeometryError(
                "DevTools dock request was rejected: "
                + json.dumps(open_result, ensure_ascii=False)
            )
        out("DevTools dock requested:", json.dumps(open_result, ensure_ascii=False))
        devtools_wait_result = wait_until(
            devtools_or_native_stop,
            "CEF ShowDevTools to return for the dock request",
            process,
            devtools_deadline,
        )
        if devtools_wait_result == "native-stop":
            raise GeometryError(
                "LLDB observed a native stop while waiting for CEF ShowDevTools; "
                "faulting-thread backtrace follows"
            )
        wait_until(
            lambda: devtools_marker_count("ci: devtools dock overlay attached") >= 1,
            "the DevTools BrowserView to attach to the CEF Views dock",
            process,
            devtools_deadline,
        )
        wait_until(
            lambda: devtools_marker_count("ci: devtools dock focus requested") >= 1,
            "the docked DevTools BrowserView to receive focus after ShowDevTools returns",
            process,
            devtools_deadline,
        )

        inspect_result = cdp.evaluate("""(() => {
          const id = typeof window.getActiveTabId === 'function'
            ? window.getActiveTabId() : null;
          if (!id || typeof window.shelterOpenDevtools !== 'function') {
            return {accepted: false, reason: 'DevTools Inspect entry point unavailable', id: id};
          }
          window.shelterOpenDevtools({tabId: id, inspect: true, x: 12, y: 12});
          return {accepted: true, id: id, x: 12, y: 12};
        })()""")
        if not isinstance(inspect_result, dict) or inspect_result.get("accepted") is not True:
            raise GeometryError(
                "DevTools Inspect request was rejected: "
                + json.dumps(inspect_result, ensure_ascii=False)
            )
        out("DevTools Inspect requested:", json.dumps(inspect_result, ensure_ascii=False))
        wait_until(
            lambda: devtools_marker_count("ci: devtools ShowDevTools returned") >= 2,
            "CEF ShowDevTools to return for the Inspect request",
            process,
            devtools_deadline,
        )
        wait_until(
            lambda: devtools_marker_count("ci: devtools dock focus requested") >= 2,
            "the docked DevTools BrowserView to receive focus after Inspect",
            process,
            devtools_deadline,
        )
        out("OK: docked DevTools opened and Inspect returned without crashing")
        acceptance_succeeded = True
        return 0
    except Exception as exc:
        diagnostic_state = json.dumps(last_ui_state[0], ensure_ascii=False)
        native_request_state = json.dumps(native_open_state[0], ensure_ascii=False)
        diagnostic = (
            f"native_open_attempts={native_open_attempts}, "
            f"accepted={native_open_accepted}, native_request={native_request_state}, "
            f"last_ui_state={diagnostic_state}"
        )
        out(f"FAIL: {exc}; {diagnostic}")
        out(f"::error title=Geometry acceptance failed::{exc}; {diagnostic}")
        try:
            stderr_stream.flush()
        except OSError:
            pass
        dump_diagnostics(
            logfile,
            stderr_file,
            native_trace_file if attach_lldb_before_devtools else "",
        )
        return 1
    finally:
        if cdp is not None:
            cdp.close()
        if native_debugger is not None:
            kill(native_debugger)
        if debug_with_lldb:
            cleanup_lldb_debuggee(
                process, stderr_file, executable, port, acceptance_succeeded
            )
        kill(process)
        try:
            os.remove(state_file)
        except OSError:
            pass
        try:
            stderr_stream.close()
        except OSError:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
