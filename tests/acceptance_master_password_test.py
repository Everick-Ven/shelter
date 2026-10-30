#!/usr/bin/env python3
"""The acceptance harness must handle mandatory master-password onboarding safely."""
from __future__ import annotations

import importlib.util
import json
import sys
import types
import unittest
from pathlib import Path
from typing import Any, Dict, List

ROOT = Path(__file__).resolve().parents[1]
# Importing the acceptance driver must not require a live CDP connection. The
# test supplies a lightweight module because websocket-client is installed by
# the platform jobs, not by the security-unit job.
if "websocket" not in sys.modules:
    websocket_stub = types.ModuleType("websocket")
    websocket_stub.create_connection = lambda *_args, **_kwargs: None
    sys.modules["websocket"] = websocket_stub

spec = importlib.util.spec_from_file_location(
    "shelter_acceptance_smoke", ROOT / "tools" / "acceptance_smoke.py"
)
assert spec is not None and spec.loader is not None
acceptance = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = acceptance
spec.loader.exec_module(acceptance)


class FakeUi:
    def __init__(self, *responses: Dict[str, Any]):
        self.responses: List[Dict[str, Any]] = list(responses)
        self.expressions: List[str] = []

    def evaluate(self, expression: str, timeout: float = 12) -> str:
        del timeout
        self.expressions.append(expression)
        if not self.responses:
            raise AssertionError("unexpected extra UI evaluation")
        return json.dumps(self.responses.pop(0))


class MasterPasswordAcceptanceTests(unittest.TestCase):
    def test_existing_wrapper_is_left_untouched(self) -> None:
        helper = getattr(acceptance, "complete_master_password_onboarding", None)
        self.assertTrue(callable(helper), "acceptance driver needs an onboarding boundary")
        ui = FakeUi({"configured": True, "form_visible": False})

        result = helper(ui, allow_setup=True)

        self.assertEqual(result["state"], "already-configured")
        self.assertEqual(len(ui.expressions), 1, "an existing wrapper must not be replaced")

    def test_required_setup_is_not_automatically_applied_to_a_profile(self) -> None:
        helper = getattr(acceptance, "complete_master_password_onboarding", None)
        self.assertTrue(callable(helper), "acceptance driver needs an onboarding boundary")
        ui = FakeUi({"configured": False, "form_visible": True, "readable": True})

        with self.assertRaises(acceptance.AcceptanceError):
            helper(ui, allow_setup=False)

        self.assertEqual(len(ui.expressions), 1, "no password is submitted without explicit CI opt-in")

    def test_disposable_ci_profile_completes_the_visible_setup_flow(self) -> None:
        helper = getattr(acceptance, "complete_master_password_onboarding", None)
        self.assertTrue(callable(helper), "acceptance driver needs an onboarding boundary")
        ui = FakeUi(
            {"configured": False, "form_visible": True, "readable": True},
            {"completed": True, "configured": True},
        )

        result = helper(ui, allow_setup=True)

        self.assertEqual(result["state"], "configured")
        self.assertEqual(len(ui.expressions), 2)
        submission = ui.expressions[1]
        self.assertIn("getElementById('masterPasswordNew')", submission)
        self.assertIn("getElementById('masterPasswordAgain')", submission)
        self.assertIn("[data-master-pass-save]", submission)
        self.assertIn("secretPasswordStatus", submission)
        self.assertIn("querySelector('[data-app-pin-close]')", submission,
                      "the harness must find the optional PIN offer shown after setup")
        self.assertIn("pinOfferClose.click()", submission,
                      "the harness must dismiss the optional PIN offer before overlay probes")
        self.assertIn("querySelector('.scrim')", submission,
                      "the harness must wait for modal teardown before continuing")
        self.assertIn("let pinOfferStillOpen = false", submission)
        self.assertIn("!pinOfferStillOpen", submission,
                      "a surviving PIN modal must fail acceptance before overlay probes")

    def test_missing_form_or_unknown_status_fails_closed(self) -> None:
        helper = getattr(acceptance, "complete_master_password_onboarding", None)
        self.assertTrue(callable(helper), "acceptance driver needs an onboarding boundary")
        for state in (
            {"configured": False, "form_visible": False, "readable": True},
            {"configured": None, "form_visible": True, "readable": True},
            {"configured": False, "form_visible": True},
        ):
            with self.subTest(state=state):
                ui = FakeUi(state)
                with self.assertRaises(acceptance.AcceptanceError):
                    helper(ui, allow_setup=True)
                self.assertEqual(len(ui.expressions), 1, "uncertain key state must never trigger setup")

    def test_unreadable_profile_never_submits_a_new_master_password(self) -> None:
        helper = getattr(acceptance, "complete_master_password_onboarding", None)
        self.assertTrue(callable(helper), "acceptance driver needs an onboarding boundary")
        ui = FakeUi(
            {"configured": False, "form_visible": True, "readable": False}
        )

        with self.assertRaises(acceptance.AcceptanceError):
            helper(ui, allow_setup=True)

        self.assertEqual(len(ui.expressions), 1, "read-only data must not get a new password wrapper")

    def test_runtime_acceptance_completes_onboarding_before_interacting(self) -> None:
        source = (ROOT / "tools" / "acceptance_smoke.py").read_text(encoding="utf-8")
        main = source[source.index("def main()") :]
        call = main.index("complete_master_password_onboarding(")
        secret_check = main.index("if not isinstance(secret_probe, dict)")
        first_interaction = main.index("window_controls_probe(ui)")
        self.assertLess(secret_check, call)
        self.assertLess(call, first_interaction)

    def test_only_explicit_disposable_ci_invocations_enable_setup(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")
        invocations = [line for line in workflow.splitlines() if "acceptance_smoke.py" in line]
        self.assertEqual(len(invocations), 4)
        self.assertTrue(all("--initialize-master-password" in line for line in invocations))

    def test_theme_probe_restores_the_original_theme_after_an_error(self) -> None:
        class FailingThemeUi:
            restoration: str | None = None

            def evaluate(self, expression: str, timeout: float = 12) -> Any:
                del timeout
                if expression == "document.documentElement.getAttribute('data-theme')":
                    return "graphite"
                if "window.setTheme" in expression:
                    self.restoration = expression
                    return True
                raise acceptance.AcceptanceError("simulated failure after a theme change")

        ui = FailingThemeUi()
        with self.assertRaises(acceptance.AcceptanceError):
            acceptance.settings_theme_switch_probe(ui)

        self.assertIsNotNone(ui.restoration, "failed probes must not poison the next retry's theme")
        self.assertIn('const theme = "graphite"', ui.restoration or "")
        self.assertIn("window.setTheme(theme)", ui.restoration or "")


if __name__ == "__main__":
    unittest.main()
