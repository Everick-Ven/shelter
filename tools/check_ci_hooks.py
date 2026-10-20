#!/usr/bin/env python3
"""Проверяет, что CI-хуки включены только в отдельной CI-сборке."""
import argparse
import sys
from pathlib import Path

TOKENS = (
    b"SHELTER_DIAG",
    b"SHELTER_SMOKE_URL",
    b"dbg.hit",
    b"dbg.clip",
    b"dbg.window",
)


def client_binaries(location: Path):
    if location.is_file():
        if sys.platform == "win32":
            location = location.parent
        else:
            return [location]
    roots = (location, location / "Release", location / "SHELTER",
             location / "SHELTER" / "Release")
    if sys.platform == "win32":
        for root in roots:
            binaries = [p for name in ("Shelter.dll", "Shelter.exe")
                        if (p := root / name).is_file()]
            if binaries:
                return binaries
    elif sys.platform == "darwin":
        for root in roots:
            app_binary = root / "SHELTER.app" / "Contents" / "MacOS" / "Shelter"
            direct_binary = root / "Contents" / "MacOS" / "Shelter"
            for candidate in (app_binary, direct_binary):
                if candidate.is_file():
                    return [candidate]
    return []


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("location", type=Path)
    parser.add_argument("expected", choices=("on", "off"))
    parser.add_argument(
        "--binary-only",
        action="store_true",
        help="inspect a packaged install without requiring its CMake cache",
    )
    args = parser.parse_args()

    expected = "ON" if args.expected == "on" else "OFF"
    if not args.binary_only:
        cache = args.location / "CMakeCache.txt"
        if not cache.is_file():
            parser.error(f"CMake cache not found: {cache}")
        cache_text = cache.read_text(encoding="utf-8", errors="replace")
        if f"SHELTER_CI_HOOKS:BOOL={expected}" not in cache_text:
            print(f"Expected SHELTER_CI_HOOKS={expected} in {cache}", file=sys.stderr)
            return 1

    binaries = client_binaries(args.location)
    if not binaries:
        print(f"SHELTER client binary not found under {args.location}", file=sys.stderr)
        return 1
    image = b"\n".join(binary.read_bytes() for binary in binaries)
    present = [token for token in TOKENS if token in image]
    if args.expected == "on" and len(present) != len(TOKENS):
        missing = [token.decode("ascii") for token in TOKENS if token not in image]
        print("CI-hook build is missing compiled hooks: " + ", ".join(missing),
              file=sys.stderr)
        return 1
    if args.expected == "off" and present:
        print("Release client contains CI-only hooks: " +
              ", ".join(token.decode("ascii") for token in present),
              file=sys.stderr)
        return 1

    print(f"SHELTER_CI_HOOKS_{args.expected.upper()}_PASS " +
          ",".join(binary.name for binary in binaries))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
