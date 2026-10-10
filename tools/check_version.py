#!/usr/bin/env python3
"""Проверяет, что версия SHELTER одинакова во всех местах, где она задана вручную.

Источник истины — set(SHELTER_VERSION ...) в CMakeLists.txt.
Запуск: python3 tools/check_version.py   (код возврата 1 при расхождении)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def find(rel, pattern, label, group=1):
    m = re.search(pattern, read(rel), re.M)
    if not m:
        return [(f"{rel} ({label})", None)]
    return [(f"{rel} ({label})", m.group(group))]


def rc_numeric(rel, key):
    m = re.search(rf"^\s*{key}\s+(\d+),(\d+),(\d+),(\d+)", read(rel), re.M)
    if not m:
        return [(f"{rel} ({key})", None)]
    return [(f"{rel} ({key})", ".".join(m.groups()[:3]) + (f" +build {m.group(4)}" if m.group(4) != "0" else ""))]


def main():
    cmake = re.search(r'set\(SHELTER_VERSION "([^"]+)"\)', read("CMakeLists.txt"))
    if not cmake:
        print("CMakeLists.txt: не найден set(SHELTER_VERSION ...)")
        return 1
    want = cmake.group(1)

    found = []
    found += find("src/common.h", r'kAppVersion\[\]\s*=\s*"([^"]+)"', "kAppVersion")
    found += find("resources/ui/index.html", r"^const VERSION = '([^']+)';", "const VERSION")
    found += find("resources/ui/index.html", r'name="description" content="SHELTER ([0-9.]+) ', "meta description")
    found += find("installer/shelter.iss", r'#define AppVersion "([^"]+)"', "AppVersion")
    found += find("installer/shelter.iss", r"/DAppVersion=([0-9.]+)", "комментарий со сборкой")
    found += find("tools/make_dmg.sh", r'VER="\$\{3:-([^}]+)\}"', "VER")
    found += find("README.md", r"версия ([0-9.]+)\)", "шапка")
    found += find("win/shelter.rc", r'VALUE "FileVersion", "([^"]+)"', "FileVersion")
    found += find("win/shelter.rc", r'VALUE "ProductVersion", "([^"]+)"', "ProductVersion")
    found += rc_numeric("win/shelter.rc", "FILEVERSION")
    found += rc_numeric("win/shelter.rc", "PRODUCTVERSION")
    for m in re.finditer(r"/DAppVersion=([0-9.]+)|make_dmg\.sh [^\n]* ([0-9]+\.[0-9]+\.[0-9]+)\s", read(".github/workflows/build.yml")):
        found.append((".github/workflows/build.yml (версия при упаковке)", m.group(1) or m.group(2)))

    bad = [(where, got) for where, got in found if got != want]
    for where, got in found:
        print(("OK   " if got == want else "FAIL ") + f"{where}: {got}")
    if bad:
        print(f"\nВерсия в CMakeLists.txt: {want}. Расхождений: {len(bad)}")
        return 1
    print(f"\nВсе {len(found)} мест совпадают с {want}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
