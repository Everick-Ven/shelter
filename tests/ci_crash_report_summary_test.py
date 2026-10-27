#!/usr/bin/env python3
"""Tests that native crash stacks are visible in GitHub check diagnostics."""
from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "crash_report_summary.py"
SPEC = importlib.util.spec_from_file_location("crash_report_summary", MODULE_PATH)
assert SPEC and SPEC.loader
summary_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(summary_module)


class NativeCrashReportSummaryTest(unittest.TestCase):
    def test_ips_summary_includes_faulting_thread_and_symbolized_frames(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "Shelter-incident.ips"
            header = {"app_name": "Shelter", "timestamp": "2026-10-27 23:26:39.00 +0000"}
            body = {
                "procName": "Shelter",
                "exception": {"type": "EXC_BAD_ACCESS", "signal": "SIGSEGV"},
                "faultingThread": 0,
                "threads": [
                    {
                        "queue": "com.apple.main-thread",
                        "frames": [
                            {
                                "imageIndex": 0,
                                "imageOffset": 4660,
                                "symbol": "CefBrowserHost::ShowDevTools",
                                "symbolLocation": 12,
                            },
                            {"imageIndex": 0, "imageOffset": 128},
                        ],
                    },
                    {
                        "frames": [{"imageIndex": 2, "imageOffset": 16, "symbol": "unrelated"}]
                    },
                ],
                "usedImages": [
                    {"name": "Chromium Embedded Framework.framework/Chromium Embedded Framework"},
                    {"name": "Shelter"},
                    {"name": "Other.framework/Other"},
                ],
            }
            report.write_text(
                json.dumps(header) + "\n" + json.dumps(body) + "\n", encoding="utf-8"
            )

            summary = summary_module.summarize_native_report(report)

            self.assertIn("EXC_BAD_ACCESS", summary)
            self.assertIn("SIGSEGV", summary)
            self.assertIn("faulting thread 0", summary)
            self.assertIn("CefBrowserHost::ShowDevTools+12", summary)
            self.assertIn("Chromium Embedded Framework.framework/Chromium Embedded Framework+0x80", summary)
            self.assertNotIn("unrelated", summary)

    def test_legacy_crash_report_summary_extracts_crashed_thread_frames(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "Shelter Helper.crash"
            report.write_text(
                """Process:               Shelter Helper [1234]
Crashed Thread:        0  Dispatch queue: com.apple.main-thread
Exception Type:        EXC_BAD_ACCESS (SIGSEGV)

Thread 0 Crashed::  Dispatch queue: com.apple.main-thread
0   libcef.dylib       0x0000000100123456 CefBrowserHost::ShowDevTools + 22
1   Shelter            0x0000000100001234 shell::OpenDevTools + 48

Thread 1:
0   libsystem_kernel.dylib 0x0000000180000000 unrelated
""",
                encoding="utf-8",
            )

            summary = summary_module.summarize_native_report(report)

            self.assertIn("EXC_BAD_ACCESS (SIGSEGV)", summary)
            self.assertIn("CefBrowserHost::ShowDevTools", summary)
            self.assertIn("shell::OpenDevTools", summary)
            self.assertNotIn("unrelated", summary)

    def test_windows_minidump_is_exposed_as_an_inventory_record(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "Shelter.exe.123.dmp"
            report.write_bytes(b"minidump")

            summary = summary_module.summarize_native_report(report)

            self.assertIn("Windows minidump", summary)
            self.assertIn("Shelter.exe.123.dmp", summary)
            self.assertIn("8 bytes", summary)

    def test_failure_reporter_emits_crash_report_annotations_from_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            reports = root / "native-reports"
            reports.mkdir()
            report = reports / "Shelter.ips"
            report.write_text(
                json.dumps({"procName": "Shelter", "exception": {"type": "EXC_BAD_ACCESS"},
                            "faultingThread": 0, "threads": [{"frames": [
                                {"imageIndex": 0, "imageOffset": 4,
                                 "symbol": "CefBrowserHost::ShowDevTools"}
                            ]}], "usedImages": [{"name": "libcef.dylib"}]})
                + "\n",
                encoding="utf-8",
            )
            summary_file = root / "step-summary.md"
            environment = os.environ.copy()
            environment["GITHUB_STEP_SUMMARY"] = str(summary_file)

            result = subprocess.run(
                [sys.executable, str(ROOT / "tools" / "report_ci_failure.py"), str(reports)],
                cwd=ROOT,
                env=environment,
                text=True,
                capture_output=True,
                check=True,
            )

            self.assertIn("::error title=SHELTER native crash report::", result.stdout)
            self.assertIn("CefBrowserHost%3A%3AShowDevTools", result.stdout)
            self.assertIn("CefBrowserHost::ShowDevTools", summary_file.read_text(encoding="utf-8"))

    def test_lldb_backtrace_is_included_even_when_buried_in_app_log(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            log = root / "geometry-app.log"
            log.write_text(
                "Process 123 stopped\n"
                "* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS\n"
                "  frame #0: 0x0000000100123456 libcef.dylib`CefBrowserHost::ShowDevTools\n"
                "  frame #1: 0x0000000100001234 Shelter`shell::OpenDevTools\n"
                + "\n".join(f"routine cleanup line {index}" for index in range(20)),
                encoding="utf-8",
            )

            result = subprocess.run(
                [sys.executable, str(ROOT / "tools" / "report_ci_failure.py"), str(log)],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=True,
            )

            self.assertIn("frame #0", result.stdout)
            self.assertIn("CefBrowserHost%3A%3AShowDevTools", result.stdout)


class NativeCrashReportWorkflowTest(unittest.TestCase):
    def test_native_report_directories_are_passed_to_failure_reporter(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        windows = workflow.index("- name: Report Windows failure diagnostics")
        macos = workflow.index("- name: Report macOS failure diagnostics")
        self.assertIn("ci-crash-reports/windows", workflow[windows:])
        self.assertIn("ci-crash-reports/macos", workflow[macos:])
        self.assertIn("tests/ci_crash_report_summary_test.py", workflow)
        self.assertIn("tests/ci_lldb_launcher_test.py", workflow)


if __name__ == "__main__":
    unittest.main()
