#!/usr/bin/python3
"""Tests for the Time Card LED status policy."""

from __future__ import annotations

import importlib.machinery
import importlib.util
import unittest
from pathlib import Path
from unittest import mock


POLICY_PATH = Path(__file__).parents[1] / "tools" / "timecard-led-policy"
LOADER = importlib.machinery.SourceFileLoader("timecard_led_policy", str(POLICY_PATH))
SPEC = importlib.util.spec_from_loader(LOADER.name, LOADER)
if SPEC is None:
    raise RuntimeError(f"could not load {POLICY_PATH}")
POLICY = importlib.util.module_from_spec(SPEC)
LOADER.exec_module(POLICY)


class MonitoringPolicyTest(unittest.TestCase):
    def test_secondary_receiver_uses_independent_monitoring_data(self) -> None:
        policy = POLICY.LedPolicy()
        monitoring = {
            "gnss": {"fixOk": False, "satellites_count": 0},
            "gnss2": {"fixOk": True, "satellites_count": 12},
        }

        self.assertEqual(policy.read_gnss2(monitoring), POLICY.GNSS_FIXED)

    def test_secondary_receiver_does_not_mirror_primary(self) -> None:
        policy = POLICY.LedPolicy()
        monitoring = {"gnss": {"fixOk": True, "satellites_count": 12}}

        self.assertEqual(policy.read_gnss2(monitoring), POLICY.GNSS_UNKNOWN)

    def test_secondary_receiver_searching_state(self) -> None:
        policy = POLICY.LedPolicy()
        monitoring = {"gnss2": {"fixOk": False, "satellites_count": 4}}

        self.assertEqual(policy.read_gnss2(monitoring), POLICY.GNSS_SEARCHING)


class OptionalLedTest(unittest.TestCase):
    def test_five_led_card_never_writes_gnss2(self) -> None:
        policy = POLICY.LedPolicy()
        card = Path("/sys/class/timecard/ocp0")

        with (
            mock.patch.object(policy, "find_card", return_value=card),
            mock.patch.object(policy, "query_oscillatord", return_value=None),
            mock.patch.object(policy, "read_gnss1", return_value=POLICY.GNSS_FAILED),
            mock.patch.object(policy, "read_sma", return_value=POLICY.SMA_DISABLED),
            mock.patch.object(policy, "find_led", return_value=None),
            mock.patch.object(policy, "set_led") as set_led,
        ):
            self.assertTrue(policy.apply_once())

        written_names = [call.args[0] for call in set_led.call_args_list]
        self.assertEqual(written_names, ["gnss1", "sma1", "sma2", "sma3", "sma4"])

    def test_six_led_card_writes_gnss2(self) -> None:
        policy = POLICY.LedPolicy()
        card = Path("/sys/class/timecard/ocp0")
        monitoring = {"gnss2": {"fixOk": True, "satellites_count": 8}}

        with (
            mock.patch.object(policy, "find_card", return_value=card),
            mock.patch.object(
                policy, "query_oscillatord", return_value=monitoring
            ),
            mock.patch.object(policy, "read_gnss1", return_value=POLICY.GNSS_FAILED),
            mock.patch.object(policy, "read_sma", return_value=POLICY.SMA_DISABLED),
            mock.patch.object(policy, "find_led", return_value=Path("/fake/gnss2")),
            mock.patch.object(policy, "set_led") as set_led,
        ):
            self.assertTrue(policy.apply_once())

        self.assertIn(mock.call("gnss2", POLICY.GNSS_FIXED), set_led.call_args_list)


if __name__ == "__main__":
    unittest.main()
