#!/usr/bin/env python3
"""Tests the macOS CI-only LLDB wrapper used to capture DevTools crash stacks."""
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "ci_lldb_launcher.py"
SPEC = importlib.util.spec_from_file_location("ci_lldb_launcher", MODULE_PATH)
assert SPEC and SPEC.loader
launcher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(launcher)


class LLDBLauncherTest(unittest.TestCase):
    def test_enabled_macos_command_captures_backtrace_and_cleans_up(self) -> None:
        command = ["/tmp/SHELTER", "--test-argument"]

        wrapped = launcher.wrap_command(
            command,
            enabled=True,
            platform_name="Darwin",
            lldb_path="/usr/bin/lldb",
        )

        self.assertEqual(
            wrapped,
            [
                "/usr/bin/lldb",
                "--batch",
                "--no-lldbinit",
                "-o",
                "run",
                "-o",
                "thread backtrace all",
                "-o",
                "process kill",
                "--",
                *command,
            ],
        )

    def test_disabled_or_non_macos_launch_keeps_native_command(self) -> None:
        command = ["Shelter.exe", "--test-argument"]
        self.assertEqual(
            launcher.wrap_command(
                command, enabled=False, platform_name="Darwin", lldb_path="lldb"
            ),
            command,
        )
        self.assertEqual(
            launcher.wrap_command(
                command, enabled=True, platform_name="Windows", lldb_path="lldb"
            ),
            command,
        )

    def test_workflow_enables_lldb_only_for_macos_geometry_acceptance(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        macos = workflow.index("  macos:")
        macos_end = workflow.index("  security-unit:", macos)
        geometry = workflow.index(
            "- name: Geometry and DevTools acceptance (CI hooks)", macos, macos_end
        )
        runtime = workflow.index(
            "- name: CI-hook runtime acceptance", geometry, macos_end
        )
        self.assertIn("SHELTER_USE_LLDB=1", workflow[geometry:runtime])
        self.assertNotIn("SHELTER_USE_LLDB=1", workflow[:macos])


if __name__ == "__main__":
    unittest.main()
