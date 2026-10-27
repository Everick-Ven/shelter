#!/usr/bin/env python3
"""Turn crash dumps from CI acceptance runs into short, readable stacks.

GitHub's blob storage (job logs, artifacts) is not reachable from every
network, but check annotations are. So besides writing a text file, this script
prints each summary as a `::error` workflow command (shows up as an annotation)
and appends it to the step summary.

Supported inputs, searched recursively under DIRECTORY:
  *.ips / *.crash  macOS crash reports (JSON .ips is parsed, .crash is trimmed)
  *.dmp            Windows WER minidumps (cdb when installed, otherwise a
                   best-effort summary built with the `minidump` package)

Best effort only: this script never exits non-zero, so it cannot hide the real
failure of the step it is diagnosing.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import traceback
from pathlib import Path

ANNOTATION_LIMIT = 3400
MAX_FRAMES = 40
CRASH_SUFFIXES = {".ips", ".crash", ".dmp"}


def escape_data(value: str) -> str:
    return value.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def escape_property(value: str) -> str:
    return escape_data(value).replace(":", "%3A").replace(",", "%2C")


# ---------------------------------------------------------------- macOS ----

def summarize_ips(path: Path) -> str:
    raw = path.read_text(encoding="utf-8", errors="replace")
    header_line, _, body = raw.partition("\n")
    try:
        data = json.loads(body)
    except ValueError:
        data = json.loads(raw)
    try:
        header = json.loads(header_line)
    except ValueError:
        header = {}

    exc = data.get("exception") or {}
    term = data.get("termination") or {}
    images = data.get("usedImages") or []
    threads = data.get("threads") or []

    lines = [
        f"{path.name}",
        f"process: {data.get('procName', header.get('app_name', '?'))} "
        f"pid={data.get('pid', '?')} parent={data.get('parentProc', '?')} "
        f"os={header.get('os_version', data.get('osVersion', {}).get('train', '?'))}",
        f"exception: {exc.get('type', '?')} signal={exc.get('signal', '?')} "
        f"subtype={exc.get('subtype', '')}".rstrip(),
    ]
    if term:
        lines.append(f"termination: {term.get('namespace', '')} {term.get('indicator', '')}".rstrip())

    index = data.get("faultingThread")
    if not isinstance(index, int) or not 0 <= index < len(threads):
        index = next((i for i, t in enumerate(threads) if t.get("triggered")), 0)
    if not threads:
        lines.append("(no thread data in report)")
        return "\n".join(lines)

    thread = threads[index]
    label = thread.get("name") or thread.get("queue") or ""
    lines.append(f"crashed thread {index} {label}".rstrip() + ":")
    for number, frame in enumerate(thread.get("frames", [])[:MAX_FRAMES]):
        image_index = frame.get("imageIndex")
        image = images[image_index] if isinstance(image_index, int) and 0 <= image_index < len(images) else {}
        image_name = image.get("name") or "?"
        symbol = frame.get("symbol")
        if symbol:
            where = f"{symbol}+{frame.get('symbolLocation', 0)}"
        else:
            where = f"+0x{frame.get('imageOffset', 0):x}"
        source = frame.get("sourceFile")
        if source:
            where += f" ({source}:{frame.get('sourceLine', '?')})"
        lines.append(f"{number:2d} {image_name} {where}")
    return "\n".join(lines)


def summarize_crash_text(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return "\n".join([path.name] + text[:70])


# -------------------------------------------------------------- Windows ----

def find_cdb() -> str | None:
    found = shutil.which("cdb")
    if found:
        return found
    for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")):
        if not base:
            continue
        for arch in ("x64", "x86"):
            candidate = Path(base) / "Windows Kits" / "10" / "Debuggers" / arch / "cdb.exe"
            if candidate.exists():
                return str(candidate)
    return None


def run_cdb(cdb: str, path: Path) -> str:
    cache = Path(os.environ.get("RUNNER_TEMP", ".")) / "crash-symbols"
    command = [
        cdb, "-z", str(path),
        "-y", f"srv*{cache}*https://msdl.microsoft.com/download/symbols",
        "-c", f".ecxr;kn {MAX_FRAMES};q",
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, errors="replace")
    out = [line.rstrip() for line in (result.stdout or "").splitlines() if line.strip()]
    # Drop the debugger banner; keep everything from the exception context on.
    start = next((i for i, line in enumerate(out) if "ExceptionAddress" in line or "Child-SP" in line), 0)
    return "cdb stack (.ecxr; kn):\n" + "\n".join(out[start:start + 90])


def python_dump_summary(path: Path) -> str:
    from minidump.minidumpfile import MinidumpFile  # imported lazily: optional dependency

    mf = MinidumpFile.parse(str(path))
    modules = sorted(
        (m.baseaddress, m.baseaddress + m.size, Path(str(m.name).replace("\\", "/")).name)
        for m in mf.modules.modules
    )

    def locate(address: int) -> str | None:
        for start, end, name in modules:
            if start <= address < end:
                return f"{name}+0x{address - start:x}"
        return None

    lines = [f"{path.name}  ({len(modules)} modules)"]
    records = getattr(getattr(mf, "exception", None), "exception_records", None) or []
    if not records:
        lines.append("no exception stream in dump")
        return "\n".join(lines)

    record = records[0]
    detail = record.ExceptionRecord
    code = detail.ExceptionCode_raw
    address = detail.ExceptionAddress
    lines.append(f"exception 0x{code:08x} tid={record.ThreadId} at 0x{address:x} = {locate(address) or 'unmapped'}")
    info = detail.ExceptionInformation or []
    if code == 0xC0000005 and len(info) >= 2:
        kind = {0: "read", 1: "write", 8: "execute (DEP)"}.get(info[0], str(info[0]))
        lines.append(f"access violation: {kind} of 0x{info[1]:x}")

    with open(path, "rb") as dump:
        dump.seek(record.ThreadContext.Rva)
        context = dump.read(record.ThreadContext.DataSize)
        if len(context) >= 0x100:
            # x64 CONTEXT: Rsp at 0x98, Rip at 0xF8.
            rsp, = struct.unpack_from("<Q", context, 0x98)
            rip, = struct.unpack_from("<Q", context, 0xF8)
            lines.append(f"rip = {locate(rip) or hex(rip)}   rsp = 0x{rsp:x}")
            thread = next((t for t in mf.threads.threads if t.ThreadId == record.ThreadId), None)
            stack = getattr(thread, "Stack", None)
            if stack and stack.StartOfMemoryRange <= rsp < stack.StartOfMemoryRange + stack.DataSize:
                dump.seek(stack.Rva + (rsp - stack.StartOfMemoryRange))
                available = stack.StartOfMemoryRange + stack.DataSize - rsp
                data = dump.read(min(available, 8 * 4096))
                lines.append("stack scan (heuristic: values pointing into modules, top first):")
                seen = 0
                for offset in range(0, len(data) - 7, 8):
                    value, = struct.unpack_from("<Q", data, offset)
                    where = locate(value)
                    if where:
                        lines.append(f"  [rsp+0x{offset:x}] {where}")
                        seen += 1
                        if seen >= MAX_FRAMES:
                            break
            else:
                lines.append("faulting thread stack memory is not in the dump")
    return "\n".join(lines)


def summarize_dmp(path: Path) -> str:
    parts: list[str] = []
    cdb = find_cdb()
    if cdb:
        try:
            parts.append(run_cdb(cdb, path))
        except Exception:  # noqa: BLE001 - diagnostics must not fail the job
            parts.append("cdb failed:\n" + traceback.format_exc(limit=3))
    else:
        parts.append("cdb not found on this runner")
    try:
        parts.append(python_dump_summary(path))
    except Exception:  # noqa: BLE001
        parts.append("minidump summary failed:\n" + traceback.format_exc(limit=4))
    return "\n".join(parts)


# ----------------------------------------------------------------- main ----

def summarize(path: Path) -> str:
    suffix = path.suffix.lower()
    try:
        if suffix == ".ips":
            return summarize_ips(path)
        if suffix == ".crash":
            return summarize_crash_text(path)
        return summarize_dmp(path)
    except Exception:  # noqa: BLE001
        return f"{path.name}: could not summarize\n" + traceback.format_exc(limit=4)


def emit(title: str, text: str, level: str = "error") -> None:
    print(f"::{level} title={escape_property(title)}::{escape_data(text[:ANNOTATION_LIMIT])}")
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            handle.write(f"### {title}\n\n```\n{text[:20000]}\n```\n\n")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory")
    parser.add_argument("--out", default="crash-stack.txt")
    parser.add_argument("--limit", type=int, default=3, help="newest N crash files to summarize")
    args = parser.parse_args(argv)

    root = Path(args.directory)
    files: list[Path] = []
    if root.is_dir():
        files = sorted(
            (p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in CRASH_SUFFIXES),
            key=lambda p: p.stat().st_mtime,
            reverse=True,
        )

    sections: list[str] = []
    if not files:
        listing = ", ".join(sorted(p.name for p in root.iterdir())[:20]) if root.is_dir() else "(directory missing)"
        message = f"no crash dumps/reports found under {root}; contents: {listing or '(empty)'}"
        sections.append(message)
        emit("No crash dump collected", message, level="warning")
    for path in files[: args.limit]:
        text = summarize(path)
        sections.append(text)
        emit(f"Crash stack: {path.name}", text)

    Path(args.out).write_text("\n\n".join(sections) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception:  # noqa: BLE001
        traceback.print_exc()
        sys.exit(0)
