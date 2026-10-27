#!/usr/bin/env python3
"""Expose concise failure logs as GitHub check annotations and step summary.

GitHub Actions stores full job logs/artifacts in blob storage that is not
always reachable from every network. Annotations and the step summary, on the
other hand, are served by the regular GitHub API, so this script distils the
important parts of each log file (error matches + tail) into both.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

from crash_report_summary import is_native_report, summarize_native_report

ERROR_RE = re.compile(
    r"\b(error|fatal|FAILED|undefined reference|cannot open|No such file"
    r"|CMake Error|is not recognized|Traceback|EXC_BAD_ACCESS|EXC_BREAKPOINT"
    r"|SIGSEGV|SIGTRAP)\b|stop reason|thread #\d+|frame #\d+|:\s*error\b",
    re.IGNORECASE,
)


def escape_command_data(value: str) -> str:
    return (
        value.replace("%", "%25")
        .replace("\r", "%0D")
        .replace("\n", "%0A")
        .replace(":", "%3A")
        .replace(",", "%2C")
    )


def summarize(lines: list[str]) -> str:
    """Keep actionable errors and a short tail within GitHub's annotation limit."""
    matches = [i for i, line in enumerate(lines) if ERROR_RE.search(line)]
    context: list[str] = []
    seen: set[int] = set()
    for idx in matches[:8]:
        for j in range(max(0, idx - 1), min(len(lines), idx + 4)):
            if j not in seen:
                seen.add(j)
                context.append(f"{j + 1}: {lines[j]}")
    tail = [f"{i + 1}: {lines[i]}" for i in range(max(0, len(lines) - 12), len(lines))]
    # GitHub truncates annotation messages at 4 KiB. Reserve room for the
    # source filename and command encoding, and never let a noisy build tail
    # erase the actual compiler/linker errors.
    budget = 3000
    if context:
        error_text = f"-- error matches ({len(matches)} lines) --\n" + "\n".join(context)
        if len(error_text) > 2200:
            error_text = error_text[:2199] + "…"
        tail_text = "-- build tail --\n" + "\n".join(tail)
        if len(tail_text) > 750:
            tail_text = "-- build tail --\n" + tail_text[-735:]
        return (error_text + "\n" + tail_text)[:budget]
    tail_text = "-- no standard error marker; build tail --\n" + "\n".join(tail)
    return tail_text[-budget:]


def main() -> int:
    # Logs routinely contain non-ASCII text; never crash on console encodings.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    emitted = False
    chunks: list[str] = []
    for raw_path in sys.argv[1:]:
        input_path = Path(raw_path)
        if input_path.is_dir():
            paths = sorted(
                path for path in input_path.rglob("*")
                if path.is_file() and is_native_report(path)
            )
        elif input_path.is_file():
            paths = [input_path]
        else:
            continue

        for path in paths:
            if is_native_report(path):
                text = f"{path.name}:\n{summarize_native_report(path)}"
                title = "SHELTER native crash report"
            else:
                try:
                    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
                except OSError as exc:
                    lines = [f"Could not read {path}: {exc}"]
                if not lines:
                    continue
                text = f"{path.name}:\n{summarize(lines)}"
                title = "SHELTER build diagnostics"
            print(
                f"::error title={title}::" + escape_command_data(text),
                flush=True,
            )
            chunks.append(text)
            emitted = True
    if not emitted:
        print(
            "::error title=SHELTER build diagnostics::"
            "No diagnostic log files were produced",
            flush=True,
        )
        return 0

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        try:
            with open(summary_path, "a", encoding="utf-8") as fh:
                fh.write("## SHELTER failure diagnostics\n\n```\n")
                fh.write("\n\n".join(chunks)[:60000])
                fh.write("\n```\n")
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
