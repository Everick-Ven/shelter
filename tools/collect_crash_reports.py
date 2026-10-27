#!/usr/bin/env python3
"""Collect recent SHELTER crash reports for native CI diagnostics.

The helper intentionally copies only reports whose names contain SHELTER and
whose timestamps are at or after the acceptance start time. Missing platform
report directories are normal and never fail the build.
"""
from __future__ import annotations

import argparse
import os
import shutil
import sys
import time
from pathlib import Path
from typing import Iterable

_MACOS_SUFFIXES = {".ips", ".crash"}
_WINDOWS_SUFFIXES = {".dmp", ".mdmp"}


def collect_reports(
    platform: str,
    source_dirs: Iterable[Path],
    output_dir: Path,
    since_epoch: float = 0,
) -> list[Path]:
    """Copy matching recent crash reports and return their destination paths."""
    suffixes = {
        "macos": _MACOS_SUFFIXES,
        "windows": _WINDOWS_SUFFIXES,
    }.get(platform)
    if suffixes is None:
        raise ValueError(f"unsupported platform: {platform}")

    output_dir.mkdir(parents=True, exist_ok=True)
    copied: list[Path] = []
    for source_dir in source_dirs:
        if not source_dir.is_dir():
            continue
        for source in sorted(source_dir.rglob("*")):
            if source.is_symlink() or not source.is_file():
                continue
            if "shelter" not in source.name.casefold():
                continue
            if source.suffix.casefold() not in suffixes:
                continue
            try:
                if source.stat().st_mtime < since_epoch:
                    continue
            except OSError:
                continue

            destination = output_dir / source.name
            collision = 1
            while destination.exists():
                destination = output_dir / (
                    f"{source.stem}-{collision}{source.suffix}"
                )
                collision += 1
            try:
                shutil.copy2(source, destination)
            except OSError as error:
                print(f"Could not copy crash report {source}: {error}", file=sys.stderr)
                continue
            copied.append(destination)
    return copied


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=("macos", "windows"))
    parser.add_argument(
        "--source-dir", action="append", default=[], type=Path,
        help="Directory to scan; may be specified more than once.",
    )
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument(
        "--since-epoch", type=float,
        default=float(os.environ.get("SHELTER_ACCEPTANCE_STARTED_AT", "0")),
        help="Ignore reports older than this Unix timestamp.",
    )
    args = parser.parse_args()

    # macOS CrashReporter may write the .ips file shortly after the process exits.
    copied: list[Path] = []
    attempts = 8 if args.platform == "macos" else 1
    for attempt in range(attempts):
        copied = collect_reports(
            args.platform, args.source_dir, args.output_dir, args.since_epoch
        )
        if copied or attempt + 1 == attempts:
            break
        time.sleep(2)

    if copied:
        for path in copied:
            print(f"SHELTER_CRASH_REPORT {path} ({path.stat().st_size} bytes)")
    else:
        print(f"No recent SHELTER {args.platform} crash reports found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
