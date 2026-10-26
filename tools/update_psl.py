#!/usr/bin/env python3
"""Regenerate src/public_suffix.h from the canonical Public Suffix List.

Downloads https://publicsuffix.org/list/public_suffix_list.dat, keeps the
effective rules (normal, `*.` wildcard, `!` exception), folds IDN labels to
punycode (hosts from GURL are already ASCII-canonicalized) and rewrites the
embedded sorted array. Run from the repository root, then run
tests/public_suffix_test.cc:

    python3 tools/update_psl.py
    c++ -std=c++17 -Wall -Wextra -Werror -fno-exceptions -I. \\
        tests/public_suffix_test.cc -o /tmp/psl-test && /tmp/psl-test

Data license: Mozilla Public License 2.0 (see the header of the datap file).
"""

from __future__ import annotations

import sys
import urllib.request
from pathlib import Path

PSL_URL = "https://publicsuffix.org/list/public_suffix_list.dat"
HEADER_BEGIN = """// SHELTER — встроенный Public Suffix List (определение регистрируемого домена).
//
// Строгий режим и allow-разрешения сравнивают «свой/чужой» домен. Ручной список
// из пары десятков двухуровневых суффиксов пропускал shared-хостинги
// (github.io, appspot.com, herokuapp.com, ...): скрипт чужого тенанта на том же
// суффиксе считался same-party и не блокировался. Здесь — полный список
// publicsuffix.org (раздел ICANN + PRIVATE, IDN свёрнут в punycode — хосты от
// GURL уже канонизированы, сравниваем ASCII с ASCII).
//
// Источник: https://publicsuffix.org/list/public_suffix_list.dat
// Лицензия данных: Mozilla Public License 2.0 (© Mozilla Foundation и авторы).
// Обновление: tools/update_psl.py (скачивает свежий список и перегенерирует
// этот файл, затем запускаются tests/public_suffix_test.cc).
//
// Заголовок без зависимостей (без CEF): тестируется обычным c++.
// Алгоритм — канонический (https://publicsuffix.org/list/):
// исключение (!...) > самое длинное совпадение (точное или *. ...) > «*».
#ifndef SHELTER_PUBLIC_SUFFIX_H_
#define SHELTER_PUBLIC_SUFFIX_H_

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace shelter {

namespace psl_detail {

// Правила отсортированы побайтово (ASCII): точные («com», «co.uk»),
// групповые («*.kobe.jp») и исключения («!city.kobe.jp») — в одном массиве.
inline constexpr const char* kRules[] = {
"""


def to_ascii(rule: str) -> str:
    prefix = ""
    body = rule
    if body.startswith("!"):
        prefix, body = "!", body[1:]
    elif body.startswith("*."):
        prefix, body = "*.", body[2:]
    labels = [label.encode("idna").decode("ascii").lower()
              for label in body.split(".")]
    return prefix + ".".join(labels)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    print(f"downloading {PSL_URL} ...")
    with urllib.request.urlopen(PSL_URL, timeout=60) as response:
        text = response.read().decode("utf-8")
    rules = set()
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        rules.add(line)
    converted = sorted({to_ascii(rule) for rule in rules})
    if len(converted) < 5000:
        print(f"ERROR: suspiciously few rules ({len(converted)}), aborting",
              file=sys.stderr)
        return 1
    for probe in ("github.io", "appspot.com", "co.uk",
                  "*.kobe.jp", "!city.kobe.jp"):
        if probe not in converted:
            print(f"ERROR: expected rule {probe!r} missing, aborting",
                  file=sys.stderr)
            return 1
    # Keep the algorithm part of the header intact, replace only the data.
    header_path = root / "src" / "public_suffix.h"
    old = header_path.read_text(encoding="utf-8")
    marker = "inline constexpr size_t kRuleCount"
    tail = old[old.index(marker):]
    out = [HEADER_BEGIN]
    out.extend(f'    "{rule}",\n' for rule in converted)
    out.append("};\n")
    out.append(tail)
    header_path.write_text("".join(out), encoding="utf-8")
    print(f"wrote {header_path} ({len(converted)} rules)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
