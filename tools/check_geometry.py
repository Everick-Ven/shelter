#!/usr/bin/env python3
"""Строгая приёмка геометрии контента (баг «страница на всё окно»).

Запускает приложение с SHELTER_SMOKE_URL='shelter://app/index.html?smoke=site'
(UI сам откроет https://example.com после boot-гейта), ждёт в shelter.log
маркер 'shell: content bounds' и проверяет, что область контента остаётся
внутри hero-зоны: x>0, y>0, w<client_w, h<client_h.

Выход 0 = геометрия верна; 1 = провал (или приложение не стартовало).
"""
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
    proc = subprocess.Popen([exe], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    deadline = time.time() + budget
    line = None
    while time.time() < deadline:
        if proc.poll() is not None:
            out("FAIL: app exited early with code", proc.returncode)
            kill(proc)
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

    kill(proc)
    if not line:
        out(f"FAIL: marker {marker!r} not found in {logfile} within {budget}s")
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
