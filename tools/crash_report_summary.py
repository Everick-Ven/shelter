#!/usr/bin/env python3
"""Extract concise native crash context for GitHub check annotations."""
from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

_NATIVE_SUFFIXES = {".ips", ".crash", ".dmp", ".mdmp"}
_MAX_FRAMES = 10


def _json_objects(text: str) -> list[dict[str, Any]]:
    """Read both single-object JSON reports and macOS JSON-lines .ips files."""
    try:
        parsed = json.loads(text)
    except (json.JSONDecodeError, ValueError):
        parsed = None
    if isinstance(parsed, dict):
        return [parsed]
    if isinstance(parsed, list):
        return [item for item in parsed if isinstance(item, dict)]

    objects: list[dict[str, Any]] = []
    for line in text.splitlines():
        try:
            item = json.loads(line)
        except (json.JSONDecodeError, ValueError):
            continue
        if isinstance(item, dict):
            objects.append(item)
    return objects


def _exception_description(payload: dict[str, Any]) -> str:
    exception = payload.get("exception", {})
    if isinstance(exception, dict):
        parts = [str(exception[key]) for key in ("type", "signal") if exception.get(key)]
        exception_text = " / ".join(parts)
    else:
        exception_text = str(exception) if exception else ""

    termination = payload.get("termination", {})
    if not exception_text and isinstance(termination, dict):
        exception_text = str(termination.get("indicator") or termination.get("namespace") or "")
    return exception_text


def _frame_description(frame: dict[str, Any], images: list[Any], index: int) -> str:
    image_index = frame.get("imageIndex")
    image: dict[str, Any] = {}
    if isinstance(image_index, int) and 0 <= image_index < len(images):
        candidate = images[image_index]
        if isinstance(candidate, dict):
            image = candidate
    module = str(image.get("name") or image.get("path") or "unknown image")
    symbol = frame.get("symbol")
    location = frame.get("symbolLocation")
    offset = frame.get("imageOffset")

    if symbol:
        result = str(symbol)
        if location not in (None, 0, "0"):
            result += f"+{location}"
        if isinstance(offset, int):
            result += f" [{module}+0x{offset:x}]"
        return f"#{index} {result}"
    if isinstance(offset, int):
        return f"#{index} {module}+0x{offset:x}"
    return f"#{index} {module}"


def _summarize_ips(text: str, path: Path) -> str:
    objects = _json_objects(text)
    payload: dict[str, Any] = {}
    for item in objects:
        payload.update(item)

    process = str(payload.get("procName") or payload.get("app_name") or path.stem)
    exception = _exception_description(payload)
    threads = payload.get("threads")
    if not isinstance(threads, list):
        threads = []
    images = payload.get("usedImages")
    if not isinstance(images, list):
        images = []

    faulting_thread = payload.get("faultingThread")
    try:
        thread_index = int(faulting_thread)
    except (TypeError, ValueError):
        thread_index = -1
    thread: dict[str, Any] = {}
    if 0 <= thread_index < len(threads) and isinstance(threads[thread_index], dict):
        thread = threads[thread_index]
    else:
        for candidate in threads:
            if isinstance(candidate, dict) and (
                candidate.get("triggered") is True or candidate.get("crashed") is True
            ):
                thread = candidate
                break

    thread_name = thread.get("name") or thread.get("queue")
    label = f"SHELTER native crash: {process}"
    if exception:
        label += f"; {exception}"
    if thread_index >= 0:
        label += f"; faulting thread {thread_index}"
    elif thread_name:
        label += f"; faulting thread {thread_name}"

    frames = thread.get("frames") if isinstance(thread, dict) else None
    stack_lines: list[str] = []
    if isinstance(frames, list):
        for frame_index, frame in enumerate(frames[:_MAX_FRAMES]):
            if isinstance(frame, dict):
                stack_lines.append(_frame_description(frame, images, frame_index))
    if stack_lines:
        return label + "\n" + "\n".join(stack_lines)

    fallback = [line.strip() for line in text.splitlines()[:5] if line.strip()]
    details = " | ".join(line[:200] for line in fallback)
    if details:
        return label + "; no structured frames found; report header: " + details
    return label + "; report contains no readable frames"


def _summarize_legacy_crash(text: str, path: Path) -> str:
    process_match = re.search(r"^Process:\s*(.+)$", text, re.MULTILINE)
    process = process_match.group(1).strip() if process_match else path.stem
    exception_match = re.search(r"^Exception Type:\s*(.+)$", text, re.MULTILINE)
    exception = exception_match.group(1).strip() if exception_match else ""
    crashed_match = re.search(r"^Crashed Thread:\s*(\d+)", text, re.MULTILINE)
    thread_id = crashed_match.group(1) if crashed_match else None

    thread_pattern = re.compile(r"^Thread\s+(\d+)\s+Crashed\b[^\n]*", re.MULTILINE)
    thread_match = thread_pattern.search(text)
    stack_lines: list[str] = []
    if thread_match:
        thread_id = thread_id or thread_match.group(1)
        tail = text[thread_match.end():]
        for line in tail.splitlines():
            if re.match(r"^Thread\s+\d+\b", line):
                break
            if re.match(r"^\s*\d+\s+\S+\s+0x[0-9a-fA-F]+\s+", line):
                stack_lines.append(line.strip())
                if len(stack_lines) >= _MAX_FRAMES:
                    break

    label = f"SHELTER native crash: {process}"
    if exception:
        label += f"; {exception}"
    if thread_id is not None:
        label += f"; faulting thread {thread_id}"
    if stack_lines:
        return label + "\n" + "\n".join(stack_lines)
    return label + "; no crashed-thread frames found"


def summarize_native_report(path: Path) -> str:
    """Return a short native stack or minidump inventory record."""
    path = Path(path)
    suffix = path.suffix.casefold()
    if suffix in {".dmp", ".mdmp"}:
        try:
            size = path.stat().st_size
        except OSError:
            size = 0
        return f"Windows minidump: {path.name} ({size} bytes; stack not symbolized)"

    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        return f"SHELTER native crash report {path.name} could not be read: {error}"

    if suffix == ".ips":
        return _summarize_ips(text, path)
    if suffix == ".crash":
        return _summarize_legacy_crash(text, path)
    return f"SHELTER native crash report: {path.name} ({len(text)} characters)"


def is_native_report(path: Path) -> bool:
    return path.suffix.casefold() in _NATIVE_SUFFIXES
