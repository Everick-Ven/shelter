#!/usr/bin/env python3
"""Fail-closed source checks for the release workflow's trust boundary.

This intentionally uses only Python's standard library so the regression runs
without installing a YAML parser or an action from the network.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = sorted((ROOT / ".github" / "workflows").glob("*.yml")) + sorted(
    (ROOT / ".github" / "workflows").glob("*.yaml")
)
PINNED_USE = re.compile(r"^\s*uses:\s*([^\s#]+)(?:\s+#.*)?$")
SECRET_REF = re.compile(r"\$\{\{\s*secrets\.([A-Za-z_][A-Za-z0-9_]*)\s*\}\}")
TOP_JOB = re.compile(r"^  ([A-Za-z0-9_-]+):\s*(?:#.*)?$")


def require(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def uncommented_lines(text: str) -> list[str]:
    # Workflow keys/actions have no quoted '#' syntax. Keep values intact but
    # discard full-line comments, which may mention old/untrusted examples.
    return [line for line in text.splitlines() if not line.lstrip().startswith("#")]


def job_block(lines: list[str], name: str) -> list[str]:
    start = next((i for i, line in enumerate(lines) if line == f"  {name}:"), -1)
    require(start >= 0, f"release job '{name}' exists")
    end = len(lines)
    for i in range(start + 1, len(lines)):
        if TOP_JOB.match(lines[i]):
            end = i
            break
    return lines[start:end]


def mapping(lines: list[str], key: str, indent: int) -> dict[str, str]:
    marker = " " * indent + key + ":"
    start = next((i for i, line in enumerate(lines) if line == marker), -1)
    require(start >= 0, f"mapping '{key}' exists at indent {indent}")
    result: dict[str, str] = {}
    child_indent = indent + 2
    for line in lines[start + 1 :]:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        spaces = len(line) - len(line.lstrip(" "))
        if spaces < child_indent:
            break
        if spaces == child_indent:
            match = re.match(" " * child_indent + r"([A-Za-z0-9_-]+):\s*(.*?)\s*$", line)
            require(match is not None, f"unrecognized '{key}' entry: {line}")
            result[match.group(1)] = match.group(2)
    return result


def check_workflow(path: Path, raw: str) -> None:
    lines = uncommented_lines(raw)
    for lineno, line in enumerate(lines, 1):
        match = PINNED_USE.match(line)
        if not match:
            continue
        action = match.group(1)
        require(
            re.search(r"@[0-9a-f]{40}$", action) is not None,
            f"{path.relative_to(ROOT)}:{lineno}: action must be pinned to a full lowercase 40-char commit SHA: {action}",
        )

    require(
        not any(re.match(r"^\s*pull_request_target:", line) for line in lines),
        f"{path.relative_to(ROOT)} must not use pull_request_target with release secrets in scope",
    )
    require("    tags: ['v*']" in lines, "release workflow must run on the version-tag push trigger")

    secret_locations: list[tuple[int, str]] = []
    for lineno, line in enumerate(lines, 1):
        for match in SECRET_REF.finditer(line):
            secret_locations.append((lineno, match.group(1)))

    release = job_block(lines, "release")
    release_first = next(i for i, line in enumerate(lines) if line == "  release:")
    release_last = release_first + len(release)
    for lineno, name in secret_locations:
        require(
            release_first < lineno - 1 < release_last,
            f"{path.relative_to(ROOT)}:{lineno}: secret '{name}' is outside the protected release job",
        )
        require(
            name in {"RELEASE_MINISIGN_SECRET_KEY_BASE64", "RELEASE_MINISIGN_PASSWORD"},
            f"unexpected workflow secret reference: {name}",
        )
    actual_secret_lines = [line for line in release if "${{ secrets." in line]
    require(
        actual_secret_lines == [
            "          RELEASE_MINISIGN_SECRET_KEY_BASE64: ${{ secrets.RELEASE_MINISIGN_SECRET_KEY_BASE64 }}",
            "          RELEASE_MINISIGN_PASSWORD: ${{ secrets.RELEASE_MINISIGN_PASSWORD }}",
        ],
        "only the two minisign secret values may be injected as environment variables in the release job",
    )

    top_permissions = mapping(lines, "permissions", 0)
    require(top_permissions == {"contents": "read"}, "workflow default permissions must remain contents: read only")

    release_if = next((line.strip()[4:] for line in release if line.strip().startswith("if: ")), "")
    require(
        release_if == "github.event_name == 'push' && startsWith(github.ref, 'refs/tags/v') && github.repository == 'Everick-Ven/shelter'",
        "release job must be restricted to v* tags in the official repository",
    )
    require(
        any(line == "    environment:" for line in release)
        and any(line == "      name: release-signing" for line in release),
        "release job must use the protected release-signing Environment",
    )
    release_permissions = mapping(release, "permissions", 4)
    require(
        release_permissions
        == {
            "actions": "read",
            "attestations": "write",
            "contents": "write",
            "id-token": "write",
        },
        f"release job permissions are not least-privilege: {release_permissions}",
    )

    block_text = "\n".join(release)
    checkout = block_text.find("uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262")
    download = block_text.find("uses: actions/download-artifact@d3f86a106a0bac45b974a628896c90dbdf5c8093")
    require(checkout >= 0 and download > checkout and "ref: main" in block_text and "persist-credentials: false" in block_text,
            "release job must check out the public key from trusted main without leaving Git credentials in the workspace")
    manifest = block_text.find("name: Generate release SHA-256 checksums")
    sign = block_text.find("name: Sign and verify release manifest with minisign")
    attest = block_text.find("uses: actions/attest-build-provenance@977bb373ede98d70efdf65b84cb5f73e068dcc2a")
    publish = block_text.find("name: Create GitHub Release")
    require(manifest >= 0 and sign > manifest and attest > sign and publish > attest,
            "release order must be manifest -> minisign sign+verify -> provenance attestation -> publish")
    require("SHA256SUMS.txt.minisig" not in block_text[manifest:sign],
            "the detached signature must not be included in its own manifest")
    require("actions/attest-build-provenance@977bb373ede98d70efdf65b84cb5f73e068dcc2a" in block_text,
            "release must attest all finalized release assets with the pinned provenance action")


def main() -> int:
    require(WORKFLOWS, "at least one GitHub Actions workflow exists")
    for path in WORKFLOWS:
        check_workflow(path, path.read_text(encoding="utf-8"))
    print(f"workflow security regression passed ({len(WORKFLOWS)} workflow(s))")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as exc:
        print(f"workflow security regression failed: {exc}", file=sys.stderr)
        sys.exit(1)
