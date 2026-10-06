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

ERROR_RE = re.compile(
    r"\b(error|fatal|FAILED|undefined reference|cannot open|No such file"
    r"|CMake Error|is not recognized|Traceback)\b|:\s*error\b",
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
    """Error matches with context first, then the tail of the file."""
    matches = [i for i, line in enumerate(lines) if ERROR_RE.search(line)]
    context: list[str] = []
    seen: set[int] = set()
    for idx in matches[:15]:
        for j in range(max(0, idx - 2), min(len(lines), idx + 5)):
            if j not in seen:
                seen.add(j)
                context.append(f"{j + 1}: {lines[j]}")
    tail = lines[-60:]
    parts = []
    if context:
        parts.append(f"-- error matches ({len(matches)} lines) --\n" + "\n".join(context))
    parts.append(f"-- tail ({len(tail)} of {len(lines)} lines) --\n" + "\n".join(tail))
    return "\n".join(parts)[-7000:]


def main() -> int:
    emitted = False
    chunks: list[str] = []
    for raw_path in sys.argv[1:]:
        path = Path(raw_path)
        if not path.is_file():
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as exc:
            lines = [f"Could not read {path}: {exc}"]
        if not lines:
            continue
        text = f"{path.name}:\n{summarize(lines)}"
        print(
            "::error title=SHELTER build diagnostics::"
            + escape_command_data(text),
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
