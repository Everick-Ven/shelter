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

    def test_attach_command_monitors_a_ready_process_for_crashes(self) -> None:
        self.assertEqual(
            launcher.attach_command(4567, lldb_path="/usr/bin/lldb"),
            [
                "/usr/bin/lldb",
                "--batch",
                "--no-lldbinit",
                "--one-line-on-crash",
                "thread backtrace all",
                "--attach-pid",
                "4567",
                "-o",
                "script print('SHELTER_LLDB_ATTACH_READY')",
                "-o",
                "continue",
            ],
        )
        self.assertTrue(
            launcher.attach_is_resuming(
                "Process 4567 stopped\nSHELTER_LLDB_ATTACH_READY\n"
                "Process 4567 resuming\n",
                4567,
            )
        )
        self.assertFalse(
            launcher.attach_is_resuming(
                "SHELTER_LLDB_ATTACH_READY\nProcess 4567 stopped\n", 4567
            )
        )
        self.assertFalse(
            launcher.attach_is_resuming(
                "(lldb) process attach --pid 4567\n", 4567
            )
        )
        self.assertEqual(
            launcher.sample_command(
                4567,
                sample_path="/usr/bin/sample",
                output_file="/tmp/geometry-native-sample.txt",
            ),
            [
                "/usr/bin/sample",
                "-mayDie",
                "-file",
                "/tmp/geometry-native-sample.txt",
                "4567",
                "5",
            ],
        )
        self.assertEqual(
            launcher.sample_command(
                4567,
                sample_path="/usr/bin/sample",
                output_file="/tmp/geometry-native-sample.txt",
                sudo_path="/usr/bin/sudo",
            )[:3],
            ["/usr/bin/sudo", "-n", "/usr/bin/sample"],
        )
        with self.assertRaises(ValueError):
            launcher.sample_command(
                4567, sample_path="", output_file="/tmp/sample.txt"
            )
        elevated = launcher.attach_command(
            4567, lldb_path="/usr/bin/lldb", sudo_path="/usr/bin/sudo"
        )
        self.assertEqual(elevated[:3], ["/usr/bin/sudo", "-n", "/usr/bin/lldb"])
        self.assertEqual(elevated[-1], "continue")
        with self.assertRaises(ValueError):
            launcher.attach_command(0, lldb_path="/usr/bin/lldb")
        with self.assertRaises(ValueError):
            launcher.attach_command(4567, lldb_path="")

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

    def test_attach_stop_is_not_reported_as_a_crash(self) -> None:
        transcript = (
            "Process 4567 stopped\n"
            "* thread #1, queue = 'com.apple.main-thread', stop reason = signal SIGSTOP\n"
            "  * frame #0: 0x0000000100123456 Shelter`main\n"
        )
        self.assertFalse(launcher.has_native_crash_stop(transcript))
        self.assertEqual(launcher.faulting_thread_summary(transcript), [])
        self.assertTrue(
            launcher.has_native_crash_stop(
                "* thread #1, stop reason = signal SIGSEGV\n"
                "  * frame #0: 0x0000000100123456 Shelter`CefBrowserHost::ShowDevTools\n"
            )
        )

    def test_faulting_thread_summary_keeps_frame_zero_and_excludes_workers(self) -> None:
        transcript = (
            "Process 4567 stopped\n"
            "* thread #2, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS\n"
            "  * frame #0: 0x0000000100123456 libcef.dylib`CefBrowserHost::ShowDevTools\n"
            "    frame #1: 0x0000000100001234 Shelter`shell::OpenDevTools\n"
            "  thread #3, name = 'ThreadPoolForegroundWorker'\n"
            "    frame #0: 0x0000000180000000 libsystem_kernel.dylib`mach_msg2_trap\n"
        )
        summary = launcher.faulting_thread_summary(transcript)
        self.assertEqual(len(summary), 3)
        self.assertIn("stop reason = EXC_BAD_ACCESS", summary[0])
        self.assertIn("CefBrowserHost::ShowDevTools", summary[1])
        self.assertIn("shell::OpenDevTools", summary[2])
        self.assertNotIn("ThreadPoolForegroundWorker", "\n".join(summary))

    def test_geometry_failure_attaches_before_devtools_and_emits_trace(self) -> None:
        source = (ROOT / "tools" / "check_geometry.py").read_text(encoding="utf-8")
        self.assertIn("faulting_thread_summary(", source)
        self.assertIn("LLDB faulting-thread backtrace", source)
        self.assertIn("SHELTER_ATTACH_LLDB_ON_DEVTOOLS", source)
        self.assertIn("continuing DevTools reproduction", source)
        self.assertIn("start_native_sample", source)
        self.assertIn("output_file=os.path.abspath(output_file)", source)
        self.assertIn("native sample finished: exit=", source)
        attach_call = source.index("native_debugger = attach_lldb_to_process(")
        sample_call = source.index("native_sampler = start_native_sample(")
        devtools_request = source.index("open_result = cdp.evaluate")
        self.assertLess(attach_call, sample_call)
        self.assertLess(sample_call, devtools_request)

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

    def test_lldb_attaches_only_as_a_failure_diagnostic_retry(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        macos = workflow.index("  macos:")
        macos_end = workflow.index("  security-unit:", macos)
        geometry = workflow.index(
            "- name: Geometry and DevTools acceptance (CI hooks)", macos, macos_end
        )
        retry = workflow.index(
            "- name: Retry macOS geometry with LLDB for crash diagnostics",
            geometry,
            macos_end,
        )
        runtime = workflow.index(
            "- name: CI-hook runtime acceptance", retry, macos_end
        )
        self.assertNotIn("SHELTER_USE_LLDB=1", workflow[geometry:retry])
        self.assertIn("failure() && steps.macos_geometry.outcome == 'failure'", workflow[retry:runtime])
        self.assertIn("github.event_name == 'workflow_dispatch'", workflow[retry:runtime])
        self.assertIn("inputs.native_devtools_diagnostics", workflow[retry:runtime])
        self.assertIn("steps.macos_geometry.outcome == 'success'", workflow[retry:runtime])
        self.assertIn("SHELTER_ATTACH_LLDB_ON_DEVTOOLS=1", workflow[retry:runtime])
        self.assertNotIn("SHELTER_USE_LLDB=1", workflow[retry:runtime])
        self.assertIn("set +e", workflow[retry:runtime])
        self.assertIn("geometry-lldb.log", workflow[retry:runtime])
        self.assertIn("geometry-lldb-trace.log", workflow[retry:runtime])
        self.assertIn("Report macOS LLDB retry stack", workflow[retry:runtime])
        self.assertIn("geometry-lldb-app.log", workflow[retry:runtime])
        self.assertIn("geometry-native-sample.log", workflow[retry:runtime])
        self.assertIn("geometry-native-sample.txt", workflow[retry:runtime])
        self.assertNotIn("SHELTER_USE_LLDB=1", workflow[:macos])


if __name__ == "__main__":
    unittest.main()
