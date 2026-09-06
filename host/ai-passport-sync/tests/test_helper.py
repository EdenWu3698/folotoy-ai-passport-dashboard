import asyncio
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

sys.path.insert(0, str(Path(__file__).parents[1]))

from passport_helper import (
    CodexQuota,
    KimiQuota,
    NUS_RX_UUID,
    PassportClient,
    build_payloads,
    build_quota_payloads,
    heat_levels,
    load_history,
    parse_codex_quota_result,
    parse_kimi_quota_response,
)


class HelperTests(unittest.TestCase):
    def test_history_deduplicates_and_ignores_agent_logs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            event = {
                "timestamp": "2026-09-05T10:00:00+08:00",
                "cwd": "/tmp/robot-lab",
                "message": {
                    "role": "assistant",
                    "id": "msg-1",
                    "usage": {"output_tokens": 120},
                    "content": "PRIVATE BODY",
                },
            }
            (root / "session.jsonl").write_text(json.dumps(event) + "\n" + json.dumps(event) + "\n")
            (root / "agent-sub.jsonl").write_text(json.dumps(event) + "\n")
            summary = load_history({"history_root": str(root)}, today=__import__("datetime").date(2026, 9, 5))
            self.assertEqual(len(summary.sessions), 1)
            self.assertEqual(summary.total_tokens, 120)
            self.assertEqual(summary.sessions[0].messages, 1)

    def test_payloads_are_compact_aggregate_only(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            event = {
                "timestamp": "2026-09-05T10:00:00+08:00",
                "cwd": "/tmp/robot-lab",
                "message": {
                    "role": "assistant",
                    "id": "msg-1",
                    "usage": {"output_tokens": 120},
                    "content": "PRIVATE BODY",
                },
            }
            (root / "session.jsonl").write_text(json.dumps(event) + "\n")
            summary = load_history({"history_root": str(root)}, today=__import__("datetime").date(2026, 9, 5))
            payloads = build_payloads(
                summary,
                {"interests": ["AI", "CODE", "HARDWARE"]},
                KimiQuota(provider="Kimi", five_hour_used=7, week_used=42,
                          daily_tokens=1200, daily_requests=3,
                          daily_output_tokens=80, valid=True),
                CodexQuota(main_used=64, spark_5h_used=3, valid=True),
            )
            encoded = json.dumps(payloads)
            self.assertNotIn("PRIVATE BODY", encoded)
            heat = next(item for item in payloads if item["cmd"] == "heat")
            self.assertEqual(len(heat["all"]), 91)
            self.assertTrue(all(len(value) == 91 for value in heat["themes"].values()))
            focus = next(item for item in payloads if item["cmd"] == "focus")
            self.assertLessEqual(focus["items"][0]["c"], 15)
            self.assertLessEqual(focus["items"][0]["d"], 30)
            kimi = next(item for item in payloads if item["cmd"] == "cc")
            self.assertEqual(kimi["five"]["used"], 7)
            self.assertEqual(kimi["week"]["used"], 42)
            self.assertEqual(kimi["today"]["tok"], 1200)
            self.assertEqual(kimi["today"]["req"], 3)
            self.assertEqual(next(item for item in payloads if item["cmd"] == "codex")["main"]["used"], 64)

    def test_kimi_quota_response(self):
        quota = parse_kimi_quota_response(
            {
                "limits": [{"detail": {
                    "limit": "100", "remaining": "94",
                    "resetTime": "2026-09-05T12:01:01Z",
                }}],
                "usage": {
                    "limit": "100", "remaining": "2",
                    "resetTime": "2026-09-09T07:01:01Z",
                },
            },
            1000,
        )
        self.assertTrue(quota.valid)
        self.assertEqual(quota.five_hour_used, 6)
        self.assertEqual(quota.week_used, 98)
        self.assertGreater(quota.five_hour_resets_at, 0)
        self.assertGreater(quota.week_resets_at, quota.five_hour_resets_at)

    def test_quota_payloads_only_include_quota_pages(self):
        payloads = build_quota_payloads(
            KimiQuota(provider="Kimi", five_hour_used=7, week_used=42, valid=True),
            CodexQuota(main_used=64, valid=True),
        )
        self.assertEqual([item["cmd"] for item in payloads], ["cc", "codex"])
        self.assertEqual(payloads[0]["five"]["used"], 7)
        self.assertEqual(payloads[1]["main"]["used"], 64)

    def test_codex_quota_response(self):
        quota = parse_codex_quota_result(
            {
                "rateLimitsByLimitId": {
                    "codex": {"primary": {"usedPercent": 64, "windowDurationMins": 10080,
                                             "resetsAt": 2000}},
                    "codex_bengalfox": {
                        "primary": {"usedPercent": 3, "resetsAt": 3000},
                        "secondary": {"usedPercent": 7, "resetsAt": 4000},
                    },
                },
                "rateLimitResetCredits": {"availableCount": 3},
            },
            1000,
        )
        self.assertTrue(quota.valid)
        self.assertEqual(quota.main_used, 64)
        self.assertEqual(quota.spark_5h_used, 3)
        self.assertEqual(quota.spark_week_used, 7)
        self.assertEqual(quota.reset_credits, 3)

    def test_heat_levels(self):
        self.assertEqual(heat_levels([0, 0]), "00")
        levels = heat_levels([0, 1, 10, 100])
        self.assertEqual(levels[0], "0")
        self.assertEqual(levels[-1], "4")


class PassportClientTests(unittest.IsolatedAsyncioTestCase):
    async def test_pairing_write_timeout_disconnects_partial_connection(self):
        async def slow_write(*_args, **_kwargs):
            await asyncio.sleep(10)

        device = type("Device", (), {"name": "Passport-TEST"})()
        advertisement = type("Advertisement", (), {"service_uuids": []})()
        fake_client = type("FakeClient", (), {})()
        fake_client.is_connected = True
        fake_client.connect = AsyncMock()
        fake_client.write_gatt_char = AsyncMock(side_effect=slow_write)
        fake_client.stop_notify = AsyncMock()
        fake_client.disconnect = AsyncMock()

        passport = PassportClient()
        passport.pairing_write_timeout = 0.01
        passport.disconnect_timeout = 0.1

        with patch("passport_helper.BleakScanner.discover", new=AsyncMock(
            return_value={"test": (device, advertisement)}
        )), patch("passport_helper.BleakClient", return_value=fake_client):
            with self.assertRaisesRegex(RuntimeError, "蓝牙配对等待超时"):
                await passport.__aenter__()

        fake_client.write_gatt_char.assert_awaited_once_with(
            NUS_RX_UUID,
            b'{"cmd":"status"}\n',
            response=True,
        )
        fake_client.stop_notify.assert_awaited_once()
        fake_client.disconnect.assert_awaited_once()
        self.assertIsNone(passport.client)


if __name__ == "__main__":
    unittest.main()
