#!/usr/bin/env python3
"""Behavioral tests for collecting only SHELTER's native crash reports."""
from __future__ import annotations

import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "collect_crash_reports.py"
SPEC = importlib.util.spec_from_file_location("collect_crash_reports", MODULE_PATH)
assert SPEC and SPEC.loader
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)


class WorkflowCrashCaptureTest(unittest.TestCase):
    def test_windows_dumps_are_enabled_before_geometry_acceptance(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        configure = workflow.index("- name: Configure Windows crash dumps")
        geometry = workflow.index("- name: Geometry and DevTools acceptance (CI hooks)")
        collect = workflow.index("- name: Collect Windows crash dumps")
        self.assertLess(configure, geometry)
        self.assertLess(geometry, collect)
        self.assertIn("HKLM:\\SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\Shelter.exe", workflow[configure:geometry])
        self.assertIn("DumpType -PropertyType DWord -Value 1", workflow[configure:geometry])
        self.assertIn("SHELTER_ACCEPTANCE_STARTED_AT", workflow[configure:geometry])
        self.assertIn("--platform windows", workflow[collect:])
        upload = workflow.index("name: SHELTER-windows-ci-logs", collect)
        self.assertIn("ci-crash-reports/windows/", workflow[upload:])

    def test_macos_reports_are_collected_and_uploaded_after_geometry(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        macos = workflow.index("  macos:")
        geometry = workflow.index(
            "- name: Geometry and DevTools acceptance (CI hooks)", macos
        )
        collect = workflow.index("- name: Collect macOS crash reports", geometry)
        upload = workflow.index("name: SHELTER-macos-ci-logs", collect)
        self.assertLess(geometry, collect)
        self.assertLess(collect, upload)
        self.assertIn("--platform macos", workflow[collect:upload])
        self.assertIn("ci-crash-reports/macos/", workflow[upload:])


class CrashReportCollectionTest(unittest.TestCase):
    def test_macos_collects_recent_shelter_reports_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "DiagnosticReports"
            output = root / "collected"
            source.mkdir()
            now = 2_000_000_000.0
            crash = source / "Shelter-2026-10-28-120000.ips"
            legacy = source / "Shelter Helper (Renderer)_2026-10-28.crash"
            unrelated = source / "Finder-2026-10-28.ips"
            old = source / "Shelter-old.ips"
            for path in (crash, legacy, unrelated, old):
                path.write_text(path.name, encoding="utf-8")
            os.utime(crash, (now, now))
            os.utime(legacy, (now, now))
            os.utime(unrelated, (now, now))
            os.utime(old, (now - 100, now - 100))

            copied = collector.collect_reports(
                "macos", [source], output, since_epoch=now - 10
            )

            self.assertEqual(
                {path.name for path in copied}, {crash.name, legacy.name}
            )
            self.assertEqual((output / crash.name).read_text(encoding="utf-8"), crash.name)
            self.assertFalse((output / unrelated.name).exists())
            self.assertFalse((output / old.name).exists())

    def test_windows_collects_recent_shelter_dumps_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "wer"
            output = root / "collected"
            source.mkdir()
            now = 2_000_000_000.0
            dump = source / "Shelter.exe.1234.dmp"
            unrelated = source / "chrome.exe.1234.dmp"
            old = source / "Shelter.exe.5678.dmp"
            for path in (dump, unrelated, old):
                path.write_bytes(path.name.encode("ascii"))
            os.utime(dump, (now, now))
            os.utime(unrelated, (now, now))
            os.utime(old, (now - 100, now - 100))

            copied = collector.collect_reports(
                "windows", [source], output, since_epoch=now - 10
            )

            self.assertEqual([path.name for path in copied], [dump.name])
            self.assertEqual((output / dump.name).read_bytes(), dump.read_bytes())
            self.assertFalse((output / unrelated.name).exists())
            self.assertFalse((output / old.name).exists())

    def test_missing_report_directories_are_nonfatal(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "collected"
            copied = collector.collect_reports(
                "macos", [root / "missing"], output, since_epoch=0
            )
            self.assertEqual(copied, [])
            self.assertTrue(output.is_dir())


if __name__ == "__main__":
    unittest.main()
