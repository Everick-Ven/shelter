#!/usr/bin/env python3
"""Fail a platform build if CEF silently configures without its sandbox."""

from __future__ import annotations

import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in {"windows", "macos"}:
        print("usage: check_sandbox_config.py CONFIGURE_LOG windows|macos", file=sys.stderr)
        return 2

    log = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
    platform = sys.argv[2]
    errors = []
    if not re.search(r"CEF sandbox:\s*ON\b", log, re.IGNORECASE):
        errors.append("CEF configure log does not report 'CEF sandbox: ON'")
    defines = "\n".join(
        line for line in log.splitlines() if "Compile defines" in line
    )
    if "CEF_USE_SANDBOX" not in defines:
        errors.append("CEF_USE_SANDBOX is missing from the target compile definitions")
    if platform == "windows" and "CEF_USE_BOOTSTRAP" not in defines:
        errors.append("CEF_USE_BOOTSTRAP is missing; the Windows bootstrap DLL model is not active")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"SHELTER_CEF_SANDBOX_CONFIG_PASS platform={platform}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
