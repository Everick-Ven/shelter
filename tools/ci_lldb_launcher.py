#!/usr/bin/env python3
"""Build a CI-only LLDB command that prints all thread frames on a stop."""
from __future__ import annotations

from collections.abc import Sequence
import re

_LAUNCHED_PID_RE = re.compile(
    r"^Process\s+(\d+)(?:\s+\([^)]*\))?\s+launched:", re.MULTILINE
)
_THREAD_HEADER_RE = re.compile(r"^\s*\*?\s*thread #\d+\b", re.IGNORECASE)
_FRAME_RE = re.compile(r"\bframe #\d+:")
_NATIVE_CRASH_STOP_RE = re.compile(
    r"\bstop reason\s*=\s*(?:EXC_[A-Z0-9_]+|"
    r"signal\s+SIG(?:ABRT|BUS|FPE|ILL|SEGV|TRAP|SYS))\b",
    re.IGNORECASE,
)


def has_native_crash_stop(output: str) -> bool:
    """Ignore LLDB's expected SIGSTOP when attaching to a live process."""
    return any(_NATIVE_CRASH_STOP_RE.search(line) for line in output.splitlines())


def faulting_thread_summary(output: str, max_frames: int = 16) -> list[str]:
    """Keep frames only for a native exception/signal, excluding attach stops."""
    lines = output.splitlines()
    thread_index = next(
        (
            index
            for index, line in enumerate(lines)
            if _THREAD_HEADER_RE.search(line)
            and _NATIVE_CRASH_STOP_RE.search(line)
        ),
        None,
    )
    if thread_index is None:
        return []

    summary = [lines[thread_index].strip()]
    for line in lines[thread_index + 1:]:
        if _THREAD_HEADER_RE.search(line):
            break
        if _FRAME_RE.search(line):
            summary.append(line.strip())
            if len(summary) > max_frames:
                break
    return summary


def attach_is_resuming(output: str, pid: int) -> bool:
    """Require both the post-attach marker and LLDB's resume confirmation."""
    return "SHELTER_LLDB_ATTACH_READY" in output and bool(
        re.search(rf"\bProcess\s+{pid}\s+resuming\b", output)
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


def sample_command(
    pid: int,
    *,
    sample_path: str,
    output_file: str,
    sudo_path: str = "",
) -> list[str]:
    """Build a bounded macOS sample command for a process under test."""
    if pid <= 0:
        raise ValueError("native sample requires a positive process ID")
    if not sample_path:
        raise ValueError("native sample requested but sample was not found on PATH")
    if not output_file:
        raise ValueError("native sample requires an output file")
    command = [sample_path, str(pid), "5", "-file", output_file]
    return [sudo_path, "-n", *command] if sudo_path else command


def attach_command(
    pid: int, *, lldb_path: str, sudo_path: str = ""
) -> list[str]:
    """Attach LLDB to a ready process and keep it monitoring native crashes."""
    if pid <= 0:
        raise ValueError("LLDB attach requires a positive process ID")
    if not lldb_path:
        raise ValueError("LLDB attach requested but lldb was not found on PATH")
    command = [
        lldb_path,
        "--batch",
        "--no-lldbinit",
        "--one-line-on-crash",
        "thread backtrace all",
        "--attach-pid",
        str(pid),
        "-o",
        "script print('SHELTER_LLDB_ATTACH_READY')",
        "-o",
        "continue",
    ]
    return [sudo_path, "-n", *command] if sudo_path else command


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
