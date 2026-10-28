#!/usr/bin/env python3
"""Build a CI-only LLDB command that prints all thread frames on a stop."""
from __future__ import annotations

from collections.abc import Sequence
import re

_LAUNCHED_PID_RE = re.compile(
    r"^Process\s+(\d+)(?:\s+\([^)]*\))?\s+launched:", re.MULTILINE
)


def inferior_pid_from_output(output: str) -> int | None:
    """Return the PID of the debugged app reported by LLDB, if present."""
    match = _LAUNCHED_PID_RE.search(output)
    return int(match.group(1)) if match else None


def inferior_pid_from_process_list(
    process_list: str, executable: str, port: int
) -> int | None:
    """Find the main SHELTER process without matching LLDB or CEF children."""
    prefixes = (
        f"{executable} --remote-debugging-port={port}",
        f'"{executable}" --remote-debugging-port={port}',
    )
    for line in process_list.splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) != 2:
            continue
        try:
            pid = int(fields[0])
        except ValueError:
            continue
        if fields[1].strip().startswith(prefixes):
            return pid
    return None


def wrap_command(
    command: Sequence[str],
    *,
    enabled: bool,
    platform_name: str,
    lldb_path: str,
) -> list[str]:
    """Wrap a macOS acceptance command in batch LLDB when explicitly enabled."""
    if not enabled or platform_name != "Darwin":
        return list(command)
    if not lldb_path:
        raise ValueError("SHELTER_USE_LLDB=1 but lldb was not found on PATH")
    return [
        lldb_path,
        "--batch",
        "--no-lldbinit",
        "--one-line-on-crash",
        "thread backtrace all",
        "-o",
        "run",
        "-o",
        "process kill",
        "--",
        *command,
    ]
