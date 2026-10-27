#!/usr/bin/env python3
"""Build a CI-only LLDB command that prints all thread frames on a stop."""
from __future__ import annotations

from collections.abc import Sequence


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
        "-o",
        "run",
        "-o",
        "thread backtrace all",
        "-o",
        "process kill",
        "--",
        *command,
    ]
