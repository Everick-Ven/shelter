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
                "--one-line-on-crash",
                "thread backtrace all",
                "-o",
                "run",
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

    def test_inferior_pid_is_parsed_for_cleanup(self) -> None:
        output = "LLDB attached\nProcess 4567 launched: '/tmp/Shelter' (x86_64)\n"
        self.assertEqual(launcher.inferior_pid_from_output(output), 4567)
        self.assertIsNone(launcher.inferior_pid_from_output("no inferior started"))

    def test_inferior_pid_fallback_selects_only_the_main_app_command(self) -> None:
        port_argument = "--remote-debugging-" + "port=1234"
        process_list = (
            f"101 /usr/bin/lldb --batch -- /tmp/SHELTER {port_argument}\n"
            f"102 /tmp/SHELTER --type=renderer {port_argument}\n"
            f"103 /tmp/SHELTER {port_argument} --remote-allow-origins=*\n"
        )
        self.assertEqual(
            launcher.inferior_pid_from_process_list(process_list, "/tmp/SHELTER", 1234),
            103,
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
