#!/usr/bin/env python3
"""Sync local Claude Code usage summaries to FoloToy AI Passport over BLE."""

from __future__ import annotations

import argparse
import asyncio
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from datetime import date, datetime, timedelta
import json
import math
from pathlib import Path
import select
import sqlite3
import subprocess
import sys
import time
from typing import Any, Iterable
import urllib.error
import urllib.request

from bleak import BleakClient, BleakScanner
from bleak.exc import BleakGATTProtocolError

NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
DEFAULT_CONFIG = Path(__file__).with_name("config.json")
MAX_TOPICS = 5
HEAT_DAYS = 91


@dataclass
class Session:
    session_id: str
    theme: str = "CLAUDE"
    first_seen: datetime | None = None
    last_seen: datetime | None = None
    output_tokens: int = 0
    messages: int = 0
    _message_ids: set[str] = field(default_factory=set, repr=False)


@dataclass
class Summary:
    sessions: list[Session]
    daily_tokens: Counter[date]
    theme_daily_tokens: dict[str, Counter[date]]
    total_tokens: int
    today: date

    @property
    def active_days(self) -> int:
        return sum(value > 0 for value in self.daily_tokens.values())


@dataclass
class KimiQuota:
    provider: str = "NO PROVIDER"
    five_hour_used: int = 0
    five_hour_resets_at: int = 0
    week_used: int = 0
    week_resets_at: int = 0
    daily_requests: int = 0
    daily_tokens: int = 0
    daily_output_tokens: int = 0
    updated_at: int = 0
    valid: bool = False


@dataclass
class CodexQuota:
    main_used: int = 0
    main_window_mins: int = 0
    main_resets_at: int = 0
    spark_5h_used: int = 0
    spark_5h_resets_at: int = 0
    spark_week_used: int = 0
    spark_week_resets_at: int = 0
    reset_credits: int = 0
    updated_at: int = 0
    valid: bool = False


def parse_timestamp(value: Any) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone()
    except ValueError:
        return None


def clean_theme(value: str) -> str:
    value = " ".join(value.replace("_", " ").replace("-", " ").split()).upper()
    return (value or "CLAUDE")[:18]


def theme_from_cwd(cwd: Any, aliases: dict[str, str]) -> str:
    if not isinstance(cwd, str) or not cwd:
        return "CLAUDE"
    expanded = str(Path(cwd).expanduser())
    name = Path(expanded).name or "CLAUDE"
    return clean_theme(aliases.get(expanded, aliases.get(name, name)))


def iter_history_files(root: Path) -> Iterable[Path]:
    if not root.exists():
        return []
    return (
        path
        for path in root.rglob("*.jsonl")
        if not path.name.startswith("agent-")
    )


def load_history(config: dict[str, Any], today: date | None = None) -> Summary:
    root = Path(config.get("history_root", "~/.claude/projects")).expanduser()
    aliases = {str(key): str(value) for key, value in config.get("theme_aliases", {}).items()}
    sessions: list[Session] = []
    daily_tokens: Counter[date] = Counter()
    theme_daily: dict[str, Counter[date]] = defaultdict(Counter)

    for path in iter_history_files(root):
        session = Session(session_id=path.stem)
        try:
            lines = path.open("r", encoding="utf-8", errors="replace")
        except OSError:
            continue
        with lines:
            for raw_line in lines:
                try:
                    event = json.loads(raw_line)
                except (json.JSONDecodeError, TypeError):
                    continue
                if not isinstance(event, dict):
                    continue
                timestamp = parse_timestamp(event.get("timestamp"))
                if timestamp:
                    session.first_seen = min(filter(None, (session.first_seen, timestamp)))
                    session.last_seen = max(filter(None, (session.last_seen, timestamp)))
                cwd = event.get("cwd")
                if cwd:
                    session.theme = theme_from_cwd(cwd, aliases)

                message = event.get("message")
                if not isinstance(message, dict) or message.get("role") != "assistant":
                    continue
                usage = message.get("usage")
                if not isinstance(usage, dict):
                    continue
                message_id = str(message.get("id") or f"line-{session.messages}")
                if message_id in session._message_ids:
                    continue
                session._message_ids.add(message_id)
                try:
                    tokens = max(0, int(usage.get("output_tokens", 0) or 0))
                except (TypeError, ValueError):
                    tokens = 0
                session.output_tokens += tokens
                session.messages += 1
                if timestamp:
                    day = timestamp.date()
                    daily_tokens[day] += tokens
                    theme_daily[session.theme][day] += tokens
        if session.first_seen or session.messages:
            sessions.append(session)

    return Summary(
        sessions=sessions,
        daily_tokens=daily_tokens,
        theme_daily_tokens=dict(theme_daily),
        total_tokens=sum(item.output_tokens for item in sessions),
        today=today or date.today(),
    )


def _quota_percent(container: Any) -> int:
    if not isinstance(container, dict):
        return 0
    try:
        limit = float(container.get("limit") or 0)
        remaining = float(container.get("remaining") or 0)
    except (TypeError, ValueError):
        return 0
    if limit <= 0:
        return 0
    return min(100, max(0, round((limit - remaining) * 100 / limit)))


def _quota_reset_epoch(container: Any) -> int:
    if not isinstance(container, dict):
        return 0
    parsed = parse_timestamp(container.get("resetTime"))
    return max(0, int(parsed.timestamp())) if parsed else 0


def parse_kimi_quota_response(body: dict[str, Any], checked_at: int,
                               provider: str = "Kimi For Coding") -> KimiQuota:
    snapshot = KimiQuota(provider=provider[:23], updated_at=checked_at)
    limits = body.get("limits")
    limits = limits if isinstance(limits, list) else []
    first = limits[0] if limits and isinstance(limits[0], dict) else {}
    five_hour = first.get("detail") if isinstance(first, dict) else {}
    week = body.get("usage")
    week = week if isinstance(week, dict) else {}
    snapshot.five_hour_used = _quota_percent(five_hour)
    snapshot.five_hour_resets_at = _quota_reset_epoch(five_hour)
    snapshot.week_used = _quota_percent(week)
    snapshot.week_resets_at = _quota_reset_epoch(week)
    snapshot.valid = bool(five_hour or week)
    return snapshot


def load_kimi_quota(config: dict[str, Any], now: datetime | None = None) -> KimiQuota:
    """Query Kimi's real 5-hour and 7-day limits using CC Switch's local provider config."""
    path = Path(config.get("cc_switch_db", "~/.cc-switch/cc-switch.db")).expanduser()
    checked_at = int((now or datetime.now().astimezone()).timestamp())
    snapshot = KimiQuota(updated_at=checked_at)
    if not path.exists():
        return snapshot
    try:
        connection = sqlite3.connect(f"file:{path}?mode=ro", uri=True, timeout=2.0)
        try:
            provider = connection.execute(
                "SELECT name, settings_config FROM providers "
                "WHERE app_type='claude' AND is_current=1 LIMIT 1"
            ).fetchone()
            daily = connection.execute(
                """
                SELECT COUNT(*),
                       COALESCE(SUM(
                           CASE WHEN input_token_semantics != 0
                                THEN MAX(input_tokens - cache_read_tokens, 0)
                                ELSE input_tokens END
                           + output_tokens + cache_read_tokens + cache_creation_tokens
                       ), 0),
                       COALESCE(SUM(output_tokens), 0)
                FROM proxy_request_logs
                WHERE app_type='claude'
                  AND date(created_at, 'unixepoch', 'localtime') = date('now', 'localtime')
                """
            ).fetchone() if provider else None
        finally:
            connection.close()
        if not provider or not provider[0] or not provider[1]:
            return snapshot
        settings = json.loads(str(provider[1]))
        environment = settings.get("env") if isinstance(settings, dict) else None
        environment = environment if isinstance(environment, dict) else {}
        base_url = str(environment.get("ANTHROPIC_BASE_URL") or "")
        token = str(environment.get("ANTHROPIC_AUTH_TOKEN") or "")
        snapshot.provider = str(provider[0])[:23]
        if "api.kimi.com/coding" not in base_url.lower() or not token:
            return snapshot
        request = urllib.request.Request(
            "https://api.kimi.com/coding/v1/usages",
            headers={"Authorization": f"Bearer {token}", "Accept": "application/json"},
        )
        timeout = min(30.0, max(1.0, float(config.get("kimi_quota_timeout", 15))))
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = json.loads(response.read(262144))
    except (OSError, sqlite3.Error, json.JSONDecodeError, TypeError, ValueError,
            urllib.error.URLError):
        return snapshot
    if not isinstance(body, dict):
        return snapshot
    parsed = parse_kimi_quota_response(body, checked_at, snapshot.provider)
    requests, tokens, output_tokens = daily or (0, 0, 0)
    parsed.daily_requests = max(0, int(requests or 0))
    parsed.daily_tokens = max(0, int(tokens or 0))
    parsed.daily_output_tokens = max(0, int(output_tokens or 0))
    return parsed


def _codex_rpc_response(process: subprocess.Popen[str], request_id: int,
                        timeout: float) -> dict[str, Any]:
    if process.stdout is None:
        raise RuntimeError("Codex app-server stdout 不可用")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([process.stdout], [], [], min(0.5, deadline - time.monotonic()))
        if not ready:
            continue
        line = process.stdout.readline()
        if not line:
            break
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(message, dict) and message.get("id") == request_id:
            if "error" in message:
                raise RuntimeError(str(message["error"]))
            result = message.get("result")
            return result if isinstance(result, dict) else {}
    raise RuntimeError("读取 Codex 配额超时")


def load_codex_quota(config: dict[str, Any], now: datetime | None = None) -> CodexQuota:
    """Read signed-in Codex account limits through the local read-only app-server RPC."""
    checked_at = int((now or datetime.now().astimezone()).timestamp())
    snapshot = CodexQuota(updated_at=checked_at)
    command = str(config.get("codex_command", "codex"))
    process: subprocess.Popen[str] | None = None
    try:
        process = subprocess.Popen(
            [command, "app-server", "--listen", "stdio://"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        if process.stdin is None:
            raise RuntimeError("Codex app-server stdin 不可用")

        def send(message: dict[str, Any]) -> None:
            assert process is not None and process.stdin is not None
            process.stdin.write(json.dumps(message, separators=(",", ":")) + "\n")
            process.stdin.flush()

        send({
            "id": 1,
            "method": "initialize",
            "params": {
                "clientInfo": {"name": "ai-passport-helper", "version": "2.0.0"},
                "capabilities": {"experimentalApi": True},
            },
        })
        _codex_rpc_response(process, 1, 8.0)
        send({"method": "initialized"})
        send({"id": 2, "method": "account/rateLimits/read"})
        result = _codex_rpc_response(process, 2, 12.0)
    except (OSError, RuntimeError, subprocess.SubprocessError):
        return snapshot
    finally:
        if process is not None:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2.0)

    return parse_codex_quota_result(result, checked_at)


def parse_codex_quota_result(result: dict[str, Any], checked_at: int) -> CodexQuota:
    snapshot = CodexQuota(updated_at=checked_at)
    limits = result.get("rateLimitsByLimitId")
    limits = limits if isinstance(limits, dict) else {}
    main = limits.get("codex") or result.get("rateLimits") or {}
    spark = limits.get("codex_bengalfox") or {}

    def window(container: Any, key: str) -> dict[str, Any]:
        value = container.get(key) if isinstance(container, dict) else None
        return value if isinstance(value, dict) else {}

    def percent(value: Any) -> int:
        try:
            return min(100, max(0, round(float(value))))
        except (TypeError, ValueError):
            return 0

    main_window = window(main, "primary")
    spark_5h = window(spark, "primary")
    spark_week = window(spark, "secondary")
    credits = result.get("rateLimitResetCredits")
    credits = credits if isinstance(credits, dict) else {}
    snapshot.main_used = percent(main_window.get("usedPercent"))
    snapshot.main_window_mins = max(0, int(main_window.get("windowDurationMins") or 0))
    snapshot.main_resets_at = max(0, int(main_window.get("resetsAt") or 0))
    snapshot.spark_5h_used = percent(spark_5h.get("usedPercent"))
    snapshot.spark_5h_resets_at = max(0, int(spark_5h.get("resetsAt") or 0))
    snapshot.spark_week_used = percent(spark_week.get("usedPercent"))
    snapshot.spark_week_resets_at = max(0, int(spark_week.get("resetsAt") or 0))
    snapshot.reset_credits = min(255, max(0, int(credits.get("availableCount") or 0)))
    snapshot.valid = bool(main_window or spark_5h or spark_week)
    return snapshot


def heat_levels(values: list[int]) -> str:
    nonzero = [value for value in values if value > 0]
    if not nonzero:
        return "0" * len(values)
    ceiling = max(nonzero)
    denominator = math.log1p(ceiling)
    return "".join(
        "0" if value <= 0 else str(min(4, max(1, math.ceil(4 * math.log1p(value) / denominator))))
        for value in values
    )


def current_streak(summary: Summary) -> int:
    cursor = summary.today
    if summary.daily_tokens[cursor] <= 0:
        cursor -= timedelta(days=1)
    count = 0
    while summary.daily_tokens[cursor] > 0:
        count += 1
        cursor -= timedelta(days=1)
    return count


def topic_stats(summary: Summary, window_days: int = 30) -> list[dict[str, Any]]:
    start = summary.today - timedelta(days=window_days - 1)
    session_counts: Counter[str] = Counter()
    active_dates: dict[str, set[date]] = defaultdict(set)
    token_counts: Counter[str] = Counter()
    for session in summary.sessions:
        if session.last_seen and session.last_seen.date() >= start:
            session_counts[session.theme] += 1
    for theme, days in summary.theme_daily_tokens.items():
        for day, tokens in days.items():
            if day >= start and tokens > 0:
                token_counts[theme] += tokens
                active_dates[theme].add(day)
    topics = sorted(token_counts, key=lambda key: (-token_counts[key], key))[:MAX_TOPICS]
    return [
        {
            "theme": theme,
            "sessions": min(15, max(1, session_counts[theme])),
            "days": min(30, len(active_dates[theme])),
            "tokens": token_counts[theme],
        }
        for theme in topics
    ]


def first_seen_by_theme(summary: Summary) -> dict[str, date]:
    result: dict[str, date] = {}
    for session in summary.sessions:
        if not session.first_seen:
            continue
        seen = session.first_seen.date()
        previous = result.get(session.theme)
        result[session.theme] = min(previous, seen) if previous else seen
    return result


def build_payloads(summary: Summary, config: dict[str, Any],
                   kimi_quota: KimiQuota | None = None,
                   codex_quota: CodexQuota | None = None) -> list[dict[str, Any]]:
    topics = topic_stats(summary)
    topic_names = [item["theme"] for item in topics]
    interests = [clean_theme(str(item)) for item in config.get("interests", [])][:3]
    while len(interests) < 3:
        interests.append(topic_names[len(interests)] if len(topic_names) > len(interests) else "CLAUDE")

    profile = {
        "cmd": "profile",
        "name": str(config.get("name", "TRAVELER"))[:16],
        "tagline": str(config.get("tagline", "EXPLORING WITH AI"))[:32],
        "into": interests,
        "links": list(config.get("links", []))[:3],
        "rev": str(config.get("revision", "A"))[:8],
        "no": str(config.get("passport_number", "0001"))[:8],
        "issued": str(config.get("issued", summary.today.isoformat())),
    }
    focus = {
        "cmd": "focus",
        "items": [{"t": item["theme"], "c": item["sessions"], "d": item["days"]} for item in topics],
    }

    start = summary.today - timedelta(days=HEAT_DAYS - 1)
    days = [start + timedelta(days=index) for index in range(HEAT_DAYS)]
    all_values = [summary.daily_tokens[day] for day in days]
    themes = {
        theme: heat_levels([summary.theme_daily_tokens.get(theme, Counter())[day] for day in days])
        for theme in topic_names
    }
    heat = {
        "cmd": "heat",
        "start": start.isoformat(),
        "all": heat_levels(all_values),
        "themes": themes,
        "sum": {
            "days": sum(value > 0 for value in all_values),
            "tok": sum(all_values),
            "streak": current_streak(summary),
            "week": sum(summary.daily_tokens[summary.today - timedelta(days=index)] for index in range(7)),
            "today": summary.daily_tokens[summary.today],
            "total": summary.total_tokens,
        },
    }
    first_seen = first_seen_by_theme(summary)
    stamps = {
        "cmd": "stamps",
        "items": [
            {"t": theme, "d": first_seen.get(theme, summary.today).isoformat()}
            for theme in topic_names
        ],
    }
    quota_payloads = build_quota_payloads(kimi_quota, codex_quota)
    return [profile, *quota_payloads, focus, heat, stamps]


def build_quota_payloads(kimi_quota: KimiQuota | None = None,
                         codex_quota: CodexQuota | None = None) -> list[dict[str, Any]]:
    kimi_quota = kimi_quota or KimiQuota()
    cc = {
        "cmd": "cc",
        "provider": kimi_quota.provider[:23],
        "five": {
            "used": kimi_quota.five_hour_used,
            "reset": kimi_quota.five_hour_resets_at,
        },
        "week": {
            "used": kimi_quota.week_used,
            "reset": kimi_quota.week_resets_at,
        },
        "today": {
            "tok": kimi_quota.daily_tokens,
            "req": kimi_quota.daily_requests,
            "out": kimi_quota.daily_output_tokens,
        },
        "updated": kimi_quota.updated_at,
        "ok": int(kimi_quota.valid),
    }
    codex_quota = codex_quota or CodexQuota()
    codex = {
        "cmd": "codex",
        "main": {
            "used": codex_quota.main_used,
            "mins": codex_quota.main_window_mins,
            "reset": codex_quota.main_resets_at,
        },
        "spark": {
            "used": codex_quota.spark_5h_used,
            "reset": codex_quota.spark_5h_resets_at,
            "week_used": codex_quota.spark_week_used,
            "week_reset": codex_quota.spark_week_resets_at,
        },
        "credits": codex_quota.reset_credits,
        "updated": codex_quota.updated_at,
        "ok": int(codex_quota.valid),
    }
    return [cc, codex]


def load_config(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise SystemExit(f"找不到配置文件：{path}") from error
    except json.JSONDecodeError as error:
        raise SystemExit(f"配置文件 JSON 无效：{error}") from error
    if not isinstance(value, dict):
        raise SystemExit("配置文件顶层必须是 JSON 对象")
    return value


class LineDecoder:
    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[dict[str, Any]]:
        self.buffer.extend(data)
        messages: list[dict[str, Any]] = []
        while b"\n" in self.buffer:
            line, _, remainder = self.buffer.partition(b"\n")
            self.buffer[:] = remainder
            if not line.strip():
                continue
            try:
                value = json.loads(line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if isinstance(value, dict):
                messages.append(value)
        return messages


class PassportClient:
    def __init__(self, device_name: str = "Passport-") -> None:
        self.device_name = device_name
        self.client: BleakClient | None = None
        self.queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue()
        self.decoder = LineDecoder()
        self.chunk_size = 180
        self.connect_timeout = 30.0
        self.pairing_write_timeout = 45.0
        self.notify_timeout = 10.0
        self.write_timeout = 10.0
        self.disconnect_timeout = 5.0

    async def __aenter__(self) -> "PassportClient":
        devices = await BleakScanner.discover(timeout=6.0, return_adv=True)
        candidate = None
        for device, advertisement in devices.values():
            advertised = [item.lower() for item in advertisement.service_uuids]
            if NUS_SERVICE_UUID in advertised or (device.name or "").startswith(self.device_name):
                candidate = device
                break
        if candidate is None:
            raise RuntimeError("未发现 AI Passport；请确认设备已开机并停留在蓝牙可连接状态")
        self.client = BleakClient(candidate, timeout=60.0)
        try:
            try:
                await asyncio.wait_for(self.client.connect(), self.connect_timeout)
            except TimeoutError as error:
                raise RuntimeError("蓝牙连接超时；将自动断开后重试") from error

            # On macOS, touching an encrypted characteristic is what triggers pairing.
            paired = False
            for attempt in range(60):
                try:
                    await asyncio.wait_for(
                        self.client.write_gatt_char(
                            NUS_RX_UUID,
                            b'{"cmd":"status"}\n',
                            response=True,
                        ),
                        self.pairing_write_timeout,
                    )
                    paired = True
                    break
                except TimeoutError as error:
                    raise RuntimeError("蓝牙配对等待超时；将自动断开后重试") from error
                except BleakGATTProtocolError as error:
                    if getattr(error, "error_code", None) not in (5, 15) and "Encryption" not in str(error):
                        raise
                    if attempt == 0:
                        print("等待蓝牙配对：请在 Mac 输入护照屏幕上的配对码…", file=sys.stderr)
                    await asyncio.sleep(1.0)
            if not paired:
                raise RuntimeError("蓝牙配对超时；请重新运行命令并输入设备配对码")

            try:
                await asyncio.wait_for(
                    self.client.start_notify(NUS_TX_UUID, self._on_notification),
                    self.notify_timeout,
                )
            except TimeoutError as error:
                raise RuntimeError("蓝牙通知订阅超时；将自动断开后重试") from error
            characteristic = self.client.services.get_characteristic(NUS_RX_UUID)
            if characteristic is not None:
                limit = int(getattr(characteristic, "max_write_without_response_size", 180))
                self.chunk_size = min(180, max(20, limit))
            await asyncio.sleep(0.3)
            return self
        except Exception:
            await self._disconnect()
            raise

    async def __aexit__(self, *_args: Any) -> None:
        await self._disconnect()

    async def _disconnect(self) -> None:
        client, self.client = self.client, None
        if client is None:
            return
        if client.is_connected:
            try:
                await asyncio.wait_for(
                    client.stop_notify(NUS_TX_UUID),
                    self.disconnect_timeout,
                )
            except Exception:
                pass
        try:
            await asyncio.wait_for(client.disconnect(), self.disconnect_timeout)
        except Exception:
            pass

    def _on_notification(self, _sender: Any, data: bytearray) -> None:
        for message in self.decoder.feed(bytes(data)):
            self.queue.put_nowait(message)

    async def send(self, payload: dict[str, Any]) -> None:
        if self.client is None:
            raise RuntimeError("设备尚未连接")
        raw = (json.dumps(payload, ensure_ascii=True, separators=(",", ":")) + "\n").encode("utf-8")
        for offset in range(0, len(raw), self.chunk_size):
            try:
                await asyncio.wait_for(
                    self.client.write_gatt_char(
                        NUS_RX_UUID,
                        raw[offset:offset + self.chunk_size],
                        response=False,
                    ),
                    self.write_timeout,
                )
            except TimeoutError as error:
                raise RuntimeError("蓝牙写入超时；将自动断开后重试") from error
            await asyncio.sleep(0.01)

    async def request(self, payload: dict[str, Any], timeout: float = 4.0) -> dict[str, Any]:
        while not self.queue.empty():
            self.queue.get_nowait()
        await self.send(payload)
        expected = str(payload.get("cmd", ""))
        deadline = asyncio.get_running_loop().time() + timeout
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise RuntimeError(f"设备没有回应 {expected or '消息'}")
            reply = await asyncio.wait_for(self.queue.get(), remaining)
            if reply.get("ack") == expected:
                if not reply.get("ok", True):
                    raise RuntimeError(f"设备拒绝 {expected}：{reply.get('error', 'unknown error')}")
                return reply


def print_preview(summary: Summary, kimi_quota: KimiQuota | None = None,
                  codex_quota: CodexQuota | None = None) -> None:
    topics = topic_stats(summary)
    print("AI Passport 本地同步预览")
    print(f"会话：{len(summary.sessions)}")
    print(f"活跃天数：{summary.active_days}")
    print(f"输出 tokens：{summary.total_tokens:,}")
    print(f"今日 tokens：{summary.daily_tokens[summary.today]:,}")
    if topics:
        print("近 30 天主题：")
        for item in topics:
            print(f"  {item['theme']}: {item['sessions']} 次会话 / {item['days']} 天 / {item['tokens']:,} tokens")
    if kimi_quota is not None and kimi_quota.valid:
        print(
            f"Kimi 配额：{kimi_quota.provider} / 5h 已用 {kimi_quota.five_hour_used}% / "
            f"7d 已用 {kimi_quota.week_used}% / 今日 {kimi_quota.daily_tokens:,} tokens / "
            f"{kimi_quota.daily_requests:,} 请求"
        )
    else:
        print("Kimi 配额：未读取到数据")
    if codex_quota is not None and codex_quota.valid:
        print(
            f"Codex 配额：主窗口已用 {codex_quota.main_used}% / "
            f"Spark 5h {codex_quota.spark_5h_used}% / "
            f"Spark 周 {codex_quota.spark_week_used}% / 重置券 {codex_quota.reset_credits}"
        )
    else:
        print("Codex 配额：未读取到数据")
    print("隐私：只读取并同步上述统计，不发送对话正文。")


async def run_status(config: dict[str, Any]) -> None:
    async with PassportClient(str(config.get("device_name_prefix", "Passport-"))) as client:
        reply = await client.request({"cmd": "status"})
        data = reply.get("data", {})
        battery = data.get("bat", {}) if isinstance(data, dict) else {}
        print(f"已连接：{data.get('name', 'AI Passport')}")
        print(f"固件：{data.get('fw', 'unknown')}，电量：{battery.get('pct', '?')}%")


async def sync_with_client(client: PassportClient, summary: Summary, config: dict[str, Any]) -> None:
    kimi_quota = load_kimi_quota(config)
    codex_quota = load_codex_quota(config)
    payloads = build_payloads(summary, config, kimi_quota, codex_quota)
    now = datetime.now().astimezone()
    offset = int((now.utcoffset() or timedelta()).total_seconds())
    await client.send({"time": [int(now.timestamp()), offset]})
    for payload in payloads:
        await client.request(payload, timeout=8.0)
        print(f"已同步：{payload['cmd']}")
    await client.send(
        {
            "total": len(summary.sessions),
            "running": 0,
            "waiting": 0,
            "msg": f"{len(summary.sessions)} sessions synced",
            "entries": [],
            "tokens": summary.total_tokens,
            "tokens_today": summary.daily_tokens[summary.today],
        }
    )
    status = await client.request({"cmd": "status"})
    data = status.get("data", {})
    print(f"同步完成：{data.get('name', 'AI Passport')} / {data.get('fw', 'unknown')}")


async def sync_quota_with_client(client: PassportClient, config: dict[str, Any]) -> None:
    kimi_quota = load_kimi_quota(config)
    codex_quota = load_codex_quota(config)
    now = datetime.now().astimezone()
    offset = int((now.utcoffset() or timedelta()).total_seconds())
    await client.send({"time": [int(now.timestamp()), offset]})
    for payload in build_quota_payloads(kimi_quota, codex_quota):
        await client.request(payload, timeout=8.0)
    print(
        f"配额同步完成：Kimi 5h {kimi_quota.five_hour_used}% / "
        f"7d {kimi_quota.week_used}% / Codex {codex_quota.main_used}%"
    )


async def run_sync(summary: Summary, config: dict[str, Any]) -> None:
    async with PassportClient(str(config.get("device_name_prefix", "Passport-"))) as client:
        await sync_with_client(client, summary, config)


async def watch(summary_loader: Any, config: dict[str, Any], quota_interval: int,
                full_interval: int) -> None:
    while True:
        try:
            async with PassportClient(str(config.get("device_name_prefix", "Passport-"))) as client:
                last_full = 0.0
                while True:
                    loop = asyncio.get_running_loop()
                    started = loop.time()
                    if last_full == 0.0 or started - last_full >= full_interval:
                        await sync_with_client(client, summary_loader(), config)
                        last_full = loop.time()
                        print(f"完整数据下次同步：{full_interval} 秒后")
                    else:
                        await sync_quota_with_client(client, config)
                    elapsed = loop.time() - started
                    await asyncio.sleep(max(0.1, quota_interval - elapsed))
        except Exception as error:
            print(f"同步失败：{error}", file=sys.stderr)
            print("5 秒后重新连接…", file=sys.stderr)
            await asyncio.sleep(5)


def main() -> None:
    parser = argparse.ArgumentParser(description="本地 Claude Code → AI Passport 蓝牙同步助手")
    parser.add_argument("command", choices=("preview", "status", "sync", "watch"))
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--interval", type=int, default=15, help="watch 的配额同步间隔（秒）")
    parser.add_argument("--full-interval", type=int, default=300,
                        help="watch 的完整数据同步间隔（秒）")
    args = parser.parse_args()
    config = load_config(args.config)
    loader = lambda: load_history(config)
    try:
        if args.command == "preview":
            summary = loader()
            print_preview(summary, load_kimi_quota(config), load_codex_quota(config))
        elif args.command == "status":
            asyncio.run(run_status(config))
        elif args.command == "sync":
            summary = loader()
            print_preview(summary, load_kimi_quota(config), load_codex_quota(config))
            asyncio.run(run_sync(summary, config))
        else:
            quota_interval = max(5, args.interval)
            full_interval = max(quota_interval, args.full_interval)
            asyncio.run(watch(loader, config, quota_interval, full_interval))
    except KeyboardInterrupt:
        print("已停止。")
    except Exception as error:
        raise SystemExit(f"错误：{error}") from error


if __name__ == "__main__":
    main()
