#!/usr/bin/env python3
"""Expose concise failure logs as GitHub check annotations for CI triage."""

from __future__ import annotations

import sys
from pathlib import Path


def escape_command_data(value: str) -> str:
    return (
        value.replace("%", "%25")
        .replace("\r", "%0D")
        .replace("\n", "%0A")
        .replace(":", "%3A")
        .replace(",", "%2C")
    )


def main() -> int:
    emitted = False
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
        tail = "\n".join(lines[-80:])[-6000:]
        message = f"{path.name}: {tail}"
        print(
            "::error title=SHELTER packaged runtime diagnostics::"
            + escape_command_data(message),
            flush=True,
        )
        emitted = True
    if not emitted:
        print(
            "::error title=SHELTER packaged runtime diagnostics::"
            "No diagnostic log files were produced",
            flush=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
