#!/usr/bin/env python3
"""Строгая приёмка геометрии контента (баг «страница на всё окно»).

Запускает приложение с SHELTER_SMOKE_URL='shelter://app/index.html?smoke=site'
(UI сам откроет https://example.com после boot-гейта), ждёт в shelter.log
маркер 'shell: content bounds' и проверяет, что область контента остаётся
внутри hero-зоны: x>0, y>0, w<client_w, h<client_h.

Выход 0 = геометрия верна; 1 = провал (или приложение не стартовало).
При любом провале печатает хвосты логов приложения, чтобы падание было видно
в CI без доступа к blob-storage.
"""
import glob
import os
import platform
import re
import subprocess
import sys
import time


def out(*a):
    print(*a, flush=True)


def user_data_dir():
    if platform.system() == "Windows":
        base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA") or "."
        return os.path.join(base, "SHELTER")
    return os.path.expanduser("~/Library/Application Support/SHELTER")


def tail_lines(path, n=80):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        return [l.rstrip("\n") for l in lines[-n:]]
    except OSError:
        return []


def dump_diagnostics(logfile, stderr_file):
    out("--- shelter.log tail ---")
    for l in tail_lines(logfile):
        out(l)
    out("--- app stderr tail ---")
    for l in tail_lines(stderr_file):
        out(l)
    if platform.system() == "Darwin":
        reports = []
        for d in (os.path.expanduser("~/Library/Logs/DiagnosticReports"),
                  "/Library/Logs/DiagnosticReports"):
            reports.extend(glob.glob(os.path.join(d, "Shelter*")))
        if reports:
            newest = max(reports, key=os.path.getmtime)
            out(f"--- newest crash report: {newest} ---")
            for l in tail_lines(newest, 120):
                out(l)


def kill(proc):
    try:
        if proc.poll() is not None:
            return
        if platform.system() == "Windows":
            subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"],
                           capture_output=True)
        else:
            proc.terminate()
            try:
                proc.wait(timeout=8)
            except Exception:
                proc.kill()
    except Exception as e:
        out("kill error:", e)


def main():
    if len(sys.argv) < 2:
        out("usage: check_geometry.py <exe> [budget_seconds]")
        sys.exit(2)
    exe = sys.argv[1]
    budget = int(sys.argv[2]) if len(sys.argv) > 2 else 45
    logfile = os.path.join(user_data_dir(), "shelter.log")
    stderr_file = "geometry-app.log"
    marker = "shell: content bounds"

    seen = 0
    if os.path.exists(logfile):
        try:
            with open(logfile, "r", encoding="utf-8", errors="replace") as f:
                seen = sum(1 for _ in f)
        except OSError:
            seen = 0

    env = dict(os.environ)
    env["SHELTER_SMOKE_URL"] = "shelter://app/index.html?smoke=site"
    out("launch:", exe)
    out("log:", logfile)
    stderr_fh = open(stderr_file, "w", encoding="utf-8", errors="replace")
    proc = subprocess.Popen([exe], env=env,
                            stdout=subprocess.DEVNULL, stderr=stderr_fh)

    deadline = time.time() + budget
    line = None
    try:
        while time.time() < deadline:
            if proc.poll() is not None:
                out("FAIL: app exited early with code", proc.returncode)
                kill(proc)
                stderr_fh.flush()
                dump_diagnostics(logfile, stderr_file)
                sys.exit(1)
            time.sleep(1)
            if not os.path.exists(logfile):
                continue
            try:
                with open(logfile, "r", encoding="utf-8", errors="replace") as f:
                    lines = f.readlines()
            except OSError:
                continue
            fresh = [l.strip() for l in lines[seen:] if marker in l]
            if fresh:
                line = fresh[-1]
                break
    finally:
        kill(proc)
        try:
            stderr_fh.close()
        except OSError:
            pass

    if not line:
        out(f"FAIL: marker {marker!r} not found in {logfile} within {budget}s")
        dump_diagnostics(logfile, stderr_file)
        sys.exit(1)

    out("geometry:", line)
    m = re.search(r"x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+) "
                  r"client_w=(\d+) client_h=(\d+)", line)
    if not m:
        out("FAIL: cannot parse geometry line")
        sys.exit(1)
    x, y, w, h, cw, ch = (int(v) for v in m.groups())
    if cw <= 0 or ch <= 0:
        out(f"FAIL: bad client size {cw}x{ch}")
        sys.exit(1)
    if x <= 0 or y <= 0 or w >= cw or h >= ch:
        out(f"FAIL: content view covers browser chrome "
            f"(x={x} y={y} w={w} h={h} client={cw}x{ch})")
        sys.exit(1)
    out(f"OK: content ({x},{y} {w}x{h}) inside client {cw}x{ch}")
    sys.exit(0)


if __name__ == "__main__":
    main()
