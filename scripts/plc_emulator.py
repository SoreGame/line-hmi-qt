#!/usr/bin/env python3
"""PLC emulator over raw TCP — fixed 1024-byte frames.

Memory map (one byte = one signal, 0=off / 1=on):

  Offset   Size   Name            Direction
  0..11    12     VALVE K1..K12   client → PLC (command); PLC echoes actual state
  12..23   12     SENSOR S1..S12  PLC → client only (client bytes ignored)
  24..1023 1000   RESERVED        always 0

Exchange: client sends 1024 bytes → PLC applies VALVE[0..11], updates sensors,
optionally replies with 1024 bytes (valve echo + sensors + zeros).

HMI health check: client sends 4 bytes [203,0,0,0] (robot-behaviour ping) →
PLC replies with one status frame (default 120 bytes = 60 int16, valves, sensors, zeros) without
changing valves. HMI treats the PLC as ready only when that frame matches
its mask. Full 1024-byte command frames still get a 1024-byte reply.

Status int16 (little-endian), same indices as PlcClient:
  45, 46     Vision 1/2 (1 = on)

Service panel (no reply):
  [99, 1]     enter service mode
  [99, 0]     leave service mode (also clears DO 0..49)
  [2, n, 1]   DO n on  (n = 0..49); applied only in service mode
  [2, n, 0]   DO n off
  In service mode the status frame int16[n] echoes DO n.

Set alarms from the server console, or from a client:
  [204, index, value, 0]  — set int16 index 51..59 to 0/1, index 50 to 0/10/20/30
  [205, 0, 0, 0]          — clear panel buttons, held e-stop and all suspicions

Кнопки корпуса, int16[50] — защёлка, как на настоящем ПЛК:
  10 — СТОП, снимается по [7, 3, 0, 0]
  20 — зелёная/СТАРТ, снимается по старту [7, 1, 1, x]
  30 — e-stop, снимается только по [7, 5, 0, 0]
Грибок зажат, int16[51]: 1 = зажат. Физическая кнопка — команды его не снимают.
Подозрения int16[52..59]. По умолчанию кадр 120 байт (60 int16).
В сервисном режиме int16[0..49] показывают DO, как и раньше.

HMI prep start used to be 10 zero bytes. That frame is still accepted and
starts the same fill simulation. Старт с пульта теперь [7, 1, 1, 0] или
[7, 1, 1, 1] — тоже запускает заполнение.

HMI commands, 4 bytes, no reply. Tag 7:
  [7, 1, 1, 0]  — старт, тензодатчик игнорируется (тот же кадр, что выбор детали 1)
  [7, 1, 1, 1]  — старт, тензодатчик учитывается
  [7, 1, 2, 0]  — деталь 2
  [7, 2, 0, 0]  — запуск основной программы (после отсчёта или «Пропустить»)
  [7, 3, 0, 0]  — стоп
  [7, 4, 0, 0]  — аварийный стоп
  [7, 5, 0, 0]  — выход из аварийного стопа

After prep-start the emulator simulates filling (for PrepOverlay):
  ~1s → ламели (byte 24 = 1)
  ~2s → гуси (byte 25 = 1)
  ~3s → сварка (bytes 26..45 = 1)
  ~4s → снова idle: int16 сварки = 1 (чек-лист перед следующим стартом)

Valve bytes other than 0/1 are ignored (client may send 0xFF to hold state on poll).

Multi-client: HMI (reads replies) and Behaviour (write-only tcp_send_comand) can
share one PLC. If a client never reads the reply, the emulator skips the reply
instead of closing the socket (avoids Broken pipe in ChaiScript).

Usage:
  python3 plc_emulator.py [--host 0.0.0.0] [--port 1502]
  python3 plc_emulator.py --client [--host 127.0.0.1] [--port 1502]
  python3 plc_emulator.py --client --set 1=1,3=0
  python3 plc_emulator.py --client --poll
  python3 plc_emulator.py --status-bytes 120
  python3 plc_emulator.py --client --panel start|stop|estop|0
  python3 plc_emulator.py --client --held 1|0
  python3 plc_emulator.py --client --suspect 52=1,53=1
  python3 plc_emulator.py --client --clear-alarms

Подозрения (int16 52..59) удобнее ставить из второго терминала, без консоли сервера:
  python3 plc_emulator.py --client --suspect 52=1 --status
  python3 plc_emulator.py --client --suspect 52=0,53=1 --status
  python3 plc_emulator.py --client --clear-alarms --status

Server console (while listening, TTY only; status-ping не печатается каждый раз):
  service 0|1
  do N=0|1
  panel stop|start|estop|0 — int16[50] (10/20/30/0)
  held 1|0  — грибок зажат, int16[51]
  suspect 52=1,53=0  — int16[52..59]
  clear
  status
  help
  quit
"""

from __future__ import annotations

import argparse
import socket
import sys
import threading
import time
from typing import Iterable

FRAME_SIZE = 1024
# --status-bytes 100..1000 (как PlcClient); кнопки/грибок/подозрения int16[50..59] нужны ≥120.
STATUS_INT_COUNT = 60
STATUS_INT_BYTES = 2
STATUS_SIZE = STATUS_INT_COUNT * STATUS_INT_BYTES
DO_COUNT = 50
SERVICE_TAG = 99
DO_TAG = 2
PREP_COMMAND_SIZE = 10
PING_FRAME = bytes((203, 0, 0, 0))
PROGRAM_SELECT_TAG = 7
SET_STATUS_TAG = 204
CLEAR_ALARMS_TAG = 205
# Временно: флаги камер в кадре статуса. 1 = камера работает.
VISION1_INT_INDEX = 45
VISION2_INT_INDEX = 46
# Предподготовка HMI: 1 = готово. Те же индексы, что PlcClient (байты).
LAMELLAE_OFFSET = 24
GEESE_OFFSET = 25
WELD_OFFSET = 26
WELD_COUNT = 20
# Готовность сварочного модуля для HMI: те же int16, что PlcClient::kWeldingReadyInts.
WELDING_READY_INTS = (10, 11, 12, 13, 14, 16)
VALVE_COUNT = 12
SENSOR_COUNT = 12
VALVE_OFFSET = 0
SENSOR_OFFSET = 12
PANEL_INT_INDEX = 50
PANEL_VALUES = {"0": 0, "stop": 10, "start": 20, "estop": 30}
PANEL_STOP = PANEL_VALUES["stop"]
PANEL_START = PANEL_VALUES["start"]
PANEL_ESTOP = PANEL_VALUES["estop"]
ESTOP_HELD_INT_INDEX = 51
SUSPICION_FIRST = 52
SUSPICION_LAST = 59
# Стейты станций перед стартом (0 = готово), как PlcClient.
LAMEL_STATE_INT = 40
GOOSE_STATE_INT = 41
BIG_GOOSE_STATE_INT = 42
WELDING_STATE_INT = 43

VALVE_NAMES = [f"K{i}" for i in range(1, VALVE_COUNT + 1)]
SENSOR_NAMES = [f"S{i}" for i in range(1, SENSOR_COUNT + 1)]

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 1502
DEFAULT_CLIENT_HOST = "127.0.0.1"
HOLD_BYTE = 0xFF
REPLY_TIMEOUT_S = 0.25
# Задержки имитации заполнения после кадра «Старт» (10 байт).
PREP_LAMELLAE_DELAY_S = 1.0
PREP_GEESE_DELAY_S = 2.0
PREP_WELD_DELAY_S = 3.0
PREP_IDLE_RESTORE_S = 4.0


def put_i16(buf: bytearray, index: int, value: int) -> None:
    off = index * STATUS_INT_BYTES
    if off < 0 or off + 1 >= len(buf):
        return
    v = int(value) & 0xFFFF
    buf[off] = v & 0xFF
    buf[off + 1] = (v >> 8) & 0xFF


def get_i16(buf: bytes, index: int) -> int:
    off = index * STATUS_INT_BYTES
    if off < 0 or off + 1 >= len(buf):
        return 0
    return int.from_bytes(buf[off : off + 2], "little", signed=True)


def recv_exact(conn: socket.socket, n: int) -> bytes | None:
    buf = bytearray()
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


def try_send_reply(conn: socket.socket, reply: bytes, peer: str) -> bool:
    """Best-effort reply. Write-only clients (Behaviour) may ignore replies;
    never tear down the connection just because the reply could not be delivered.
    """
    try:
        conn.settimeout(REPLY_TIMEOUT_S)
        conn.sendall(reply)
        return True
    except (BrokenPipeError, ConnectionResetError, TimeoutError, OSError) as exc:
        print(f"[plc] {peer} reply skipped ({exc})", flush=True)
        return False
    finally:
        try:
            conn.settimeout(None)
        except OSError:
            pass


def format_bits(names: Iterable[str], values: Iterable[int]) -> str:
    return " ".join(f"{name}={int(v)}" for name, v in zip(names, values))


def is_alarm_index(index: int) -> bool:
    return index in (PANEL_INT_INDEX, ESTOP_HELD_INT_INDEX) or SUSPICION_FIRST <= index <= SUSPICION_LAST


def check_alarm_value(index: int, value: int) -> None:
    if index == PANEL_INT_INDEX:
        if value not in PANEL_VALUES.values():
            raise ValueError(f"int16[{PANEL_INT_INDEX}] must be 0/10/20/30: {value}")
    elif value not in (0, 1):
        raise ValueError(f"alarm value must be 0 or 1: {value}")


def parse_panel(text: str) -> int:
    text = text.strip()
    if text in PANEL_VALUES:
        return PANEL_VALUES[text]
    value = int(text)
    check_alarm_value(PANEL_INT_INDEX, value)
    return value


class PlcState:
    """In-memory valve/sensor/alarm/prep state shared by all TCP clients."""

    def __init__(self) -> None:
        self.valves = [0] * VALVE_COUNT
        self.sensors = [0] * SENSOR_COUNT
        self.panel = 0
        self.held = 0
        self.suspicions = [0] * (SUSPICION_LAST - SUSPICION_FIRST + 1)
        # idle: int16 сварки = 1 для чек-листа. filling: байты предподготовки 0→1.
        self.phase = "idle"
        self.prep_lamellae = 0
        self.prep_geese = 0
        self.prep_weld = 0
        self.service_mode = False
        self.digital_outputs = [0] * DO_COUNT
        self._lock = threading.Lock()
        self._prep_epoch = 0

    def apply_valve_commands(self, frame: bytes) -> list[tuple[str, int, int]]:
        """Apply bytes[0:12] as valve commands. Returns list of (name, old, new)."""
        if len(frame) == 4 and frame[0] == 203:
            return []
        changes: list[tuple[str, int, int]] = []
        with self._lock:
            for i in range(VALVE_COUNT):
                if VALVE_OFFSET + i >= len(frame):
                    break
                raw = frame[VALVE_OFFSET + i]
                if raw not in (0, 1):
                    continue
                old = self.valves[i]
                if old != raw:
                    self.valves[i] = raw
                    changes.append((VALVE_NAMES[i], old, raw))
            self._update_sensors_locked()
        return changes

    def set_service_mode(self, on: bool) -> bool:
        with self._lock:
            was = self.service_mode
            self.service_mode = bool(on)
            if not self.service_mode:
                self.digital_outputs = [0] * DO_COUNT
            return was

    def service_on(self) -> bool:
        with self._lock:
            return self.service_mode

    def set_do(self, index: int, value: int) -> str | None:
        if value not in (0, 1):
            return f"DO value must be 0 or 1: {value}"
        if index < 0 or index >= DO_COUNT:
            return f"DO index out of range 0..{DO_COUNT - 1}: {index}"
        with self._lock:
            if not self.service_mode:
                return "ignored (not in service mode)"
            self.digital_outputs[index] = value
        return None

    def set_alarm_int(self, index: int, value: int) -> None:
        check_alarm_value(index, value)
        if not is_alarm_index(index):
            raise ValueError(
                f"alarm index must be {PANEL_INT_INDEX}, {ESTOP_HELD_INT_INDEX} or {SUSPICION_FIRST}..{SUSPICION_LAST}: {index}"
            )
        with self._lock:
            if index == PANEL_INT_INDEX:
                self.panel = value
            elif index == ESTOP_HELD_INT_INDEX:
                self.held = value
            else:
                self.suspicions[index - SUSPICION_FIRST] = value

    def clear_alarms(self) -> None:
        with self._lock:
            self.panel = 0
            self.held = 0
            for i in range(len(self.suspicions)):
                self.suspicions[i] = 0

    def alarm_snapshot(self) -> tuple[int, int, list[int]]:
        with self._lock:
            return self.panel, self.held, list(self.suspicions)

    def release_panel(self, value: int) -> bool:
        """HMI ответил командой — снять защёлку, если в int16[50] именно это значение."""
        with self._lock:
            if self.panel != value:
                return False
            self.panel = 0
            return True

    def begin_prep_fill(self) -> None:
        """HMI прислал 10-байтный Старт — имитируем набор ламелей/гусей/сварки."""
        with self._lock:
            self._prep_epoch += 1
            epoch = self._prep_epoch
            self.phase = "filling"
            self.prep_lamellae = 0
            self.prep_geese = 0
            self.prep_weld = 0
        print(
            f"[plc] prep-fill start (lamellae@{PREP_LAMELLAE_DELAY_S:.0f}s "
            f"geese@{PREP_GEESE_DELAY_S:.0f}s weld@{PREP_WELD_DELAY_S:.0f}s)",
            flush=True,
        )

        def step(delay: float, fn) -> None:
            def run() -> None:
                time.sleep(delay)
                with self._lock:
                    if self._prep_epoch != epoch:
                        return
                    fn()
            threading.Thread(target=run, daemon=True, name="plc-prep-fill").start()

        def set_lamellae() -> None:
            self.prep_lamellae = 1
            print("[plc] prep-fill: ламели готовы", flush=True)

        def set_geese() -> None:
            self.prep_geese = 1
            print("[plc] prep-fill: гуси готовы", flush=True)

        def set_weld() -> None:
            self.prep_weld = 1
            print("[plc] prep-fill: сварка готова", flush=True)

        def restore_idle() -> None:
            self.phase = "idle"
            self.prep_lamellae = 1
            self.prep_geese = 1
            self.prep_weld = 1
            print("[plc] prep-fill: idle (сварка int16 снова 1)", flush=True)

        step(PREP_LAMELLAE_DELAY_S, set_lamellae)
        step(PREP_GEESE_DELAY_S, set_geese)
        step(PREP_WELD_DELAY_S, set_weld)
        step(PREP_IDLE_RESTORE_S, restore_idle)

    def _update_sensors_locked(self) -> None:
        """S_i follows K_i so valve commands are visible in the sensor reply."""
        for i in range(SENSOR_COUNT):
            self.sensors[i] = self.valves[i]

    def build_status(self) -> bytes:
        """100-byte status frame for the HMI ping (50 int16)."""
        with self._lock:
            out = bytearray(STATUS_SIZE)
            for i, v in enumerate(self.valves):
                if VALVE_OFFSET + i < STATUS_SIZE:
                    out[VALVE_OFFSET + i] = v
            for i, s in enumerate(self.sensors):
                if SENSOR_OFFSET + i < STATUS_SIZE:
                    out[SENSOR_OFFSET + i] = s
            put_i16(out, VISION1_INT_INDEX, 1)
            put_i16(out, VISION2_INT_INDEX, 1)

            if self.phase == "filling":
                # Байты предподготовки для PrepOverlay (ещё идут с 0 к 1).
                out[LAMELLAE_OFFSET] = self.prep_lamellae
                out[GEESE_OFFSET] = self.prep_geese
                for i in range(WELD_COUNT):
                    if WELD_OFFSET + i < STATUS_SIZE:
                        out[WELD_OFFSET + i] = self.prep_weld
            else:
                # Idle: чек-лист HMI смотрит int16 сварки == 1.
                for index in WELDING_READY_INTS:
                    put_i16(out, index, 1)

            # Станции: 0 = готово к старту.
            put_i16(out, LAMEL_STATE_INT, 0)
            put_i16(out, GOOSE_STATE_INT, 0)
            put_i16(out, BIG_GOOSE_STATE_INT, 0)
            put_i16(out, WELDING_STATE_INT, 0)
            put_i16(out, PANEL_INT_INDEX, self.panel)
            put_i16(out, ESTOP_HELD_INT_INDEX, self.held)
            for i, v in enumerate(self.suspicions):
                put_i16(out, SUSPICION_FIRST + i, v)

            if self.service_mode:
                for i, v in enumerate(self.digital_outputs):
                    put_i16(out, i, v)
        return bytes(out)

    def build_reply(self) -> tuple[bytes, list[int], list[int]]:
        with self._lock:
            out = bytearray(FRAME_SIZE)
            for i, v in enumerate(self.valves):
                out[VALVE_OFFSET + i] = v
            for i, s in enumerate(self.sensors):
                out[SENSOR_OFFSET + i] = s
            valves = list(self.valves)
            sensors = list(self.sensors)
        return bytes(out), valves, sensors


def format_alarms(panel: int, held: int, suspicions: list[int]) -> str:
    parts = [f"panel={panel}", f"held={held}"]
    for i, v in enumerate(suspicions):
        if v:
            parts.append(f"{SUSPICION_FIRST + i}={v}")
    if len(parts) == 2:
        parts.append("suspects=none")
    return " ".join(parts)


def handle_tag7(frame: bytes, peer: str, state: PlcState) -> None:
    sub = frame[1]
    is_start = frame[1] == 1 and frame[2] == 1 and frame[3] in (0, 1)
    if is_start:
        flag = "ignore-on" if frame[3] == 0 else "ignore-off"
        print(f"[plc] {peer} hmi start {flag} {list(frame)}", flush=True)
        if state.release_panel(PANEL_START):
            print("[plc] panel: СТАРТ (20) снят", flush=True)
        state.begin_prep_fill()
        return
    names = {
        1: "program",
        2: "main-start",
        3: "stop",
        4: "e-stop",
        5: "e-stop-exit",
    }
    name = names.get(sub, "unknown")
    print(f"[plc] {peer} hmi {name} {list(frame)}", flush=True)
    if sub == 3 and state.release_panel(PANEL_STOP):
        print("[plc] panel: СТОП (10) снят", flush=True)
    if sub == 5 and state.release_panel(PANEL_ESTOP):
        print("[plc] panel: e-stop (30) снят", flush=True)


def try_consume_pending(
    conn: socket.socket, pending: bytearray, peer: str, state: PlcState
) -> str:
    """'consumed' | 'need_more' | 'full_frame'."""
    if not pending:
        return "need_more"

    if pending[0] == SERVICE_TAG:
        if len(pending) < 2:
            return "need_more"
        on = pending[1] != 0
        del pending[:2]
        state.set_service_mode(on)
        print(f"[plc] {peer} service {'on' if on else 'off'} [99, {int(on)}]", flush=True)
        return "consumed"

    if pending[0] == DO_TAG:
        if len(pending) < 3:
            return "need_more"
        n = pending[1]
        v = pending[2]
        frame = [2, n, v]
        del pending[:3]
        err = state.set_do(n, v)
        if err:
            print(f"[plc] {peer} DO {list(frame)} {err}", flush=True)
        else:
            print(f"[plc] {peer} DO {n}={'on' if v else 'off'}", flush=True)
        return "consumed"

    if (
        len(pending) == PREP_COMMAND_SIZE
        or (
            len(pending) == PREP_COMMAND_SIZE + len(PING_FRAME)
            and bytes(pending[PREP_COMMAND_SIZE:]) == PING_FRAME
        )
    ):
        frame = bytes(pending[:PREP_COMMAND_SIZE])
        del pending[:PREP_COMMAND_SIZE]
        print(f"[plc] {peer} prep-start {frame.hex()}", flush=True)
        state.begin_prep_fill()
        return "consumed"

    if len(pending) >= 4 and pending[0] == PROGRAM_SELECT_TAG:
        frame = bytes(pending[:4])
        del pending[:4]
        handle_tag7(frame, peer, state)
        return "consumed"

    if len(pending) >= 4 and pending[0] == 203:
        frame = bytes(pending[:4])
        del pending[:4]
        state.apply_valve_commands(frame)
        reply = state.build_status()
        panel, held, suspects = state.alarm_snapshot()
        alarm_key = (panel, held, tuple(suspects), state.service_on())
        if getattr(state, "_last_logged_alarms", None) != alarm_key:
            state._last_logged_alarms = alarm_key
            print(
                f"[plc] {peer} status {STATUS_SIZE}B  {format_alarms(panel, held, suspects)}"
                f"{' service' if state.service_on() else ''}",
                flush=True,
            )
        try_send_reply(conn, reply, peer)
        return "consumed"

    if len(pending) >= 4 and pending[0] == SET_STATUS_TAG:
        frame = bytes(pending[:4])
        del pending[:4]
        index = frame[1]
        value = frame[2]
        try:
            state.set_alarm_int(index, value)
            print(f"[plc] {peer} set int16[{index}]={value}", flush=True)
        except ValueError as exc:
            print(f"[plc] {peer} set rejected: {exc}", flush=True)
        return "consumed"

    if len(pending) >= 4 and pending[0] == CLEAR_ALARMS_TAG:
        del pending[:4]
        state.clear_alarms()
        print(f"[plc] {peer} alarms cleared", flush=True)
        return "consumed"

    if len(pending) >= FRAME_SIZE:
        return "full_frame"
    return "need_more"


def handle_client(conn: socket.socket, addr: tuple[str, int], state: PlcState) -> None:
    peer = f"{addr[0]}:{addr[1]}"
    print(f"[plc] connected {peer}", flush=True)
    try:
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass

    pending = bytearray()

    try:
        while True:
            action = try_consume_pending(conn, pending, peer, state)
            if action == "consumed":
                continue
            if action == "full_frame":
                frame = bytes(pending[:FRAME_SIZE])
                del pending[:FRAME_SIZE]
                changes = state.apply_valve_commands(frame)
                reply, valves, sensors = state.build_reply()
                if changes:
                    detail = ", ".join(
                        f"{name}:{old}->{new}" for name, old, new in changes
                    )
                    print(f"[plc] {peer} valves changed: {detail}", flush=True)
                else:
                    print(
                        f"[plc] {peer} OK  {format_bits(VALVE_NAMES, valves)} | "
                        f"{format_bits(SENSOR_NAMES, sensors)}",
                        flush=True,
                    )
                try_send_reply(conn, reply, peer)
                continue

            chunk = conn.recv(FRAME_SIZE - len(pending) if pending else FRAME_SIZE)
            if not chunk:
                print(f"[plc] disconnected {peer}", flush=True)
                return
            pending.extend(chunk)
    except (ConnectionResetError, BrokenPipeError, OSError) as exc:
        print(f"[plc] {peer} error: {exc}", flush=True)
    finally:
        try:
            conn.close()
        except OSError:
            pass


def parse_alarm_spec(spec: str) -> dict[int, int]:
    """Parse '51=1' or '52=1,53=0' → {index: value}."""
    result: dict[int, int] = {}
    if not spec.strip():
        return result
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "=" not in part:
            raise ValueError(f"bad alarm item (want N=0|1): {part!r}")
        left, right = part.split("=", 1)
        idx = int(left.strip())
        val = int(right.strip())
        if not is_alarm_index(idx):
            raise ValueError(
                f"alarm index must be {PANEL_INT_INDEX}, {ESTOP_HELD_INT_INDEX} or {SUSPICION_FIRST}..{SUSPICION_LAST}: {idx}"
            )
        check_alarm_value(idx, val)
        result[idx] = val
    return result


def run_server_console(state: PlcState, stop: threading.Event) -> None:
    if not sys.stdin.isatty():
        return
    print(
        "[plc] console: service 0|1 | do N=0|1 | panel stop|start|estop|0 | held 0|1 | suspect 52=1 | "
        "clear | status | help | quit",
        flush=True,
    )
    while not stop.is_set():
        try:
            line = input().strip()
        except EOFError:
            return
        except KeyboardInterrupt:
            stop.set()
            return
        if not line:
            continue
        if line in ("q", "quit", "exit"):
            stop.set()
            return
        if line in ("h", "help", "?"):
            print(
                "  service 0|1            — сервисный режим\n"
                "  do N=0|1               — DO 0..49 (только в сервисе)\n"
                "  panel stop|start|estop|0 — кнопки корпуса, int16[50] = 10/20/30/0\n"
                "  held 0|1               — грибок зажат, int16[51]\n"
                "  suspect 52=1,53=0      — подозрения int16[52..59]\n"
                "  clear                  — сбросить кнопки, грибок и подозрения\n"
                "  status                 — текущие аварии и сервис\n"
                "  quit                   — остановить сервер",
                flush=True,
            )
            continue
        if line == "status":
            mode = "on" if state.service_on() else "off"
            print(f"[plc] service={mode} {format_alarms(*state.alarm_snapshot())}", flush=True)
            continue
        if line.startswith("service "):
            try:
                val = int(line.split(None, 1)[1].strip())
                state.set_service_mode(bool(val))
                print(f"[plc] service={'on' if val else 'off'}", flush=True)
            except (ValueError, IndexError) as exc:
                print(f"[plc] error: {exc}", flush=True)
            continue
        if line.startswith("do "):
            try:
                spec = line.split(None, 1)[1].strip()
                left, right = spec.split("=", 1)
                err = state.set_do(int(left.strip()), int(right.strip()))
                if err:
                    print(f"[plc] error: {err}", flush=True)
                else:
                    print(f"[plc] {spec}", flush=True)
            except (ValueError, IndexError) as exc:
                print(f"[plc] error: {exc}", flush=True)
            continue
        if line == "clear":
            state.clear_alarms()
            print("[plc] alarms cleared", flush=True)
            continue
        if line.startswith("panel "):
            try:
                val = parse_panel(line.split(None, 1)[1])
                state.set_alarm_int(PANEL_INT_INDEX, val)
                print(f"[plc] panel={val}", flush=True)
            except (ValueError, IndexError) as exc:
                print(f"[plc] error: {exc}", flush=True)
            continue
        if line.startswith("held "):
            try:
                val = int(line.split(None, 1)[1].strip())
                state.set_alarm_int(ESTOP_HELD_INT_INDEX, val)
                print(f"[plc] held={val}", flush=True)
            except (ValueError, IndexError) as exc:
                print(f"[plc] error: {exc}", flush=True)
            continue
        if line.startswith("suspect "):
            try:
                for idx, val in parse_alarm_spec(line.split(None, 1)[1]).items():
                    state.set_alarm_int(idx, val)
                print(f"[plc] {format_alarms(*state.alarm_snapshot())}", flush=True)
            except (ValueError, IndexError) as exc:
                print(f"[plc] error: {exc}", flush=True)
            continue
        print("[plc] unknown; try: help", flush=True)


def run_server(host: str, port: int) -> None:
    state = PlcState()
    stop = threading.Event()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(16)
        srv.settimeout(0.5)
        print(
            f"[plc] listening on {host}:{port} "
            f"(frame={FRAME_SIZE}, status={STATUS_SIZE}B/{STATUS_INT_COUNT} int16, multi-client)",
            flush=True,
        )
        print(
            f"[plc] map: valves@{VALVE_OFFSET}..{VALVE_OFFSET + VALVE_COUNT - 1} "
            f"sensors@{SENSOR_OFFSET}..{SENSOR_OFFSET + SENSOR_COUNT - 1} "
            f"panel@int16[{PANEL_INT_INDEX}] held@int16[{ESTOP_HELD_INT_INDEX}] "
            f"suspects@int16[{SUSPICION_FIRST}..{SUSPICION_LAST}]",
            flush=True,
        )
        print(
            "[plc] write-only clients (Behaviour tcp_send_comand) supported; "
            "unread replies are skipped, socket stays open",
            flush=True,
        )
        console = threading.Thread(
            target=run_server_console,
            args=(state, stop),
            daemon=True,
            name="plc-console",
        )
        console.start()
        while not stop.is_set():
            try:
                conn, addr = srv.accept()
            except TimeoutError:
                continue
            threading.Thread(
                target=handle_client,
                args=(conn, addr, state),
                daemon=True,
                name=f"plc-{addr[0]}:{addr[1]}",
            ).start()
        print("[plc] stopped", flush=True)


def parse_set_spec(spec: str) -> dict[int, int]:
    """Parse '1=1,3=0' → {0:1, 2:0} (1-based valve index → value)."""
    result: dict[int, int] = {}
    if not spec.strip():
        return result
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "=" not in part:
            raise ValueError(f"bad --set item (want N=0|1): {part!r}")
        left, right = part.split("=", 1)
        idx = int(left.strip())
        val = int(right.strip())
        if idx < 1 or idx > VALVE_COUNT:
            raise ValueError(f"valve index out of range 1..{VALVE_COUNT}: {idx}")
        if val not in (0, 1):
            raise ValueError(f"valve value must be 0 or 1: {val}")
        result[idx - 1] = val
    return result


def make_command_frame(valves: list[int] | None = None, *, hold: bool = False) -> bytes:
    """Build a 1024-byte command frame.

    hold=True writes 0xFF into the valve region so the PLC leaves valves unchanged
    (only 0/1 are applied).
    """
    frame = bytearray(FRAME_SIZE)
    if hold:
        for i in range(VALVE_COUNT):
            frame[VALVE_OFFSET + i] = HOLD_BYTE
    elif valves is not None:
        for i, v in enumerate(valves):
            frame[VALVE_OFFSET + i] = v
    return bytes(frame)


def exchange(
    host: str,
    port: int,
    valves: list[int] | None = None,
    *,
    hold: bool = False,
) -> tuple[list[int], list[int]]:
    """Send one frame, return (valves, sensors) from reply."""
    frame = make_command_frame(valves, hold=hold)
    with socket.create_connection((host, port), timeout=5.0) as sock:
        sock.sendall(frame)
        reply = recv_exact(sock, FRAME_SIZE)
        if reply is None:
            raise ConnectionError("PLC closed connection before full reply")
    out_valves = [reply[VALVE_OFFSET + i] for i in range(VALVE_COUNT)]
    out_sensors = [reply[SENSOR_OFFSET + i] for i in range(SENSOR_COUNT)]
    return out_valves, out_sensors


def send_control(host: str, port: int, frame: bytes) -> None:
    with socket.create_connection((host, port), timeout=5.0) as sock:
        sock.sendall(frame)


def send_status_sets(host: str, port: int, pairs: dict[int, int]) -> None:
    for index, value in pairs.items():
        send_control(host, port, bytes((SET_STATUS_TAG, index, value, 0)))


def send_clear_alarms(host: str, port: int) -> None:
    send_control(host, port, bytes((CLEAR_ALARMS_TAG, 0, 0, 0)))


def poll_status(host: str, port: int) -> bytes:
    with socket.create_connection((host, port), timeout=5.0) as sock:
        sock.sendall(PING_FRAME)
        reply = recv_exact(sock, STATUS_SIZE)
        if reply is None:
            raise ConnectionError("PLC closed connection before status reply")
    return reply


def print_state(label: str, valves: list[int], sensors: list[int]) -> None:
    print(f"[client] {label}")
    print(f"  valves  {format_bits(VALVE_NAMES, valves)}")
    print(f"  sensors {format_bits(SENSOR_NAMES, sensors)}")


def print_status_frame(label: str, frame: bytes) -> None:
    panel = get_i16(frame, PANEL_INT_INDEX)
    held = get_i16(frame, ESTOP_HELD_INT_INDEX)
    suspects = [
        get_i16(frame, i) for i in range(SUSPICION_FIRST, SUSPICION_LAST + 1)
    ]
    print(f"[client] {label} ({len(frame)}B)")
    print(f"  {format_alarms(panel, held, suspects)}")
    print(
        f"  vision1={get_i16(frame, VISION1_INT_INDEX)} "
        f"vision2={get_i16(frame, VISION2_INT_INDEX)}"
    )


def run_client(
    host: str,
    port: int,
    set_spec: str | None,
    oneshot: bool,
    *,
    panel: int | None = None,
    held: int | None = None,
    suspect_spec: str | None = None,
    clear_alarms: bool = False,
    status_poll: bool = False,
) -> int:
    desired = [0] * VALVE_COUNT

    if clear_alarms:
        send_clear_alarms(host, port)
        print("[client] alarms cleared", flush=True)

    alarm_pairs: dict[int, int] = {}
    if panel is not None:
        alarm_pairs[PANEL_INT_INDEX] = panel
    if held is not None:
        alarm_pairs[ESTOP_HELD_INT_INDEX] = held
    if suspect_spec:
        alarm_pairs.update(parse_alarm_spec(suspect_spec))
    if alarm_pairs:
        send_status_sets(host, port, alarm_pairs)
        print(
            "[client] set "
            + " ".join(f"{i}={v}" for i, v in sorted(alarm_pairs.items())),
            flush=True,
        )

    if status_poll or (oneshot and (clear_alarms or alarm_pairs) and not set_spec):
        frame = poll_status(host, port)
        print_status_frame("status", frame)
        if oneshot and not set_spec:
            return 0

    if oneshot:
        if set_spec:
            current, _ = exchange(host, port, hold=True)
            desired[:] = current
            for i, v in parse_set_spec(set_spec).items():
                desired[i] = v
            valves, sensors = exchange(host, port, desired)
        else:
            if clear_alarms or alarm_pairs or status_poll:
                return 0
            valves, sensors = exchange(host, port, hold=True)
        print_state("exchange" if set_spec else "poll", valves, sensors)
        return 0

    print(
        "Commands: poll | status | set <N>=<0|1>[,...] | panel stop|start|estop|0 | "
        "held 0|1 | suspect 52=1[,...] | clear | quit\n"
        f"Connected target {host}:{port}",
        flush=True,
    )
    try:
        valves, sensors = exchange(host, port, hold=True)
        desired[:] = valves
        print_state("sync", valves, sensors)
    except OSError as exc:
        print(f"[client] connect failed: {exc}", file=sys.stderr)
        return 1

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if not line:
            continue
        if line in ("q", "quit", "exit"):
            return 0
        if line == "poll":
            try:
                valves, sensors = exchange(host, port, desired)
                desired[:] = valves
                print_state("poll", valves, sensors)
            except OSError as exc:
                print(f"[client] error: {exc}", file=sys.stderr)
            continue
        if line == "status":
            try:
                print_status_frame("status", poll_status(host, port))
            except OSError as exc:
                print(f"[client] error: {exc}", file=sys.stderr)
            continue
        if line == "clear":
            try:
                send_clear_alarms(host, port)
                print_status_frame("status", poll_status(host, port))
            except OSError as exc:
                print(f"[client] error: {exc}", file=sys.stderr)
            continue
        if line.startswith("panel "):
            try:
                val = parse_panel(line.split(None, 1)[1])
                send_status_sets(host, port, {PANEL_INT_INDEX: val})
                print_status_frame("status", poll_status(host, port))
            except (ValueError, OSError, IndexError) as exc:
                print(f"[client] error: {exc}", file=sys.stderr)
            continue
        if line.startswith("held "):
            try:
                val = int(line.split(None, 1)[1].strip())
                check_alarm_value(ESTOP_HELD_INT_INDEX, val)
                send_status_sets(host, port, {ESTOP_HELD_INT_INDEX: val})
                print_status_frame("status", poll_status(host, port))
            except (ValueError, OSError, IndexError) as exc:
                print(f"[client] error: {exc}", file=sys.stderr)
            continue
        if line.startswith("suspect "):
            try:
                pairs = parse_alarm_spec(line.split(None, 1)[1])
                send_status_sets(host, port, pairs)
                print_status_frame("status", poll_status(host, port))
            except (ValueError, OSError, IndexError) as exc:
                print(f"[client] error: {exc}", file=sys.stderr)
            continue
        if line.startswith("set "):
            try:
                for i, v in parse_set_spec(line[4:]).items():
                    desired[i] = v
                valves, sensors = exchange(host, port, desired)
                desired[:] = valves
                print_state("set", valves, sensors)
            except (ValueError, OSError) as exc:
                print(f"[client] error: {exc}", file=sys.stderr)
            continue
        print(
            "unknown command; try: poll | status | set 1=1 | panel stop | "
            "held 1 | suspect 52=1 | clear | quit"
        )


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="PLC raw-TCP emulator (1024-byte frames)")
    p.add_argument("--host", default=None, help="bind/connect host")
    p.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"TCP port (default {DEFAULT_PORT})")
    p.add_argument("--client", action="store_true", help="run as test client instead of server")
    p.add_argument("--set", dest="set_spec", default=None, help="client: valve set e.g. 1=1,3=0")
    p.add_argument("--poll", action="store_true", help="client: single exchange then exit")
    p.add_argument(
        "--panel",
        type=parse_panel,
        default=None,
        help="client: int16[50] кнопки корпуса: stop|start|estop|0 (или 10/20/30)",
    )
    p.add_argument(
        "--held",
        type=int,
        choices=(0, 1),
        default=None,
        help="client: int16[51] грибок зажат: 1/0",
    )
    p.add_argument(
        "--status-bytes",
        type=int,
        default=120,
        help="размер кадра статуса, 100..1000; кнопки/грибок/подозрения int16[50..59] нужны ≥120",
    )
    p.add_argument(
        "--suspect",
        dest="suspect_spec",
        default=None,
        help="client: set suspicions e.g. 52=1,53=1",
    )
    p.add_argument(
        "--clear-alarms",
        action="store_true",
        help="client: clear panel, held e-stop and suspicions",
    )
    p.add_argument(
        "--status",
        action="store_true",
        help="client: ping [203,0,0,0] and print status",
    )
    return p


def main(argv: list[str] | None = None) -> int:
    global STATUS_INT_COUNT, STATUS_SIZE
    args = build_arg_parser().parse_args(argv)
    if not (100 <= args.status_bytes <= 1000) or args.status_bytes % 2:
        print("--status-bytes: чётное число 100..1000", file=sys.stderr)
        return 2
    STATUS_INT_COUNT = args.status_bytes // STATUS_INT_BYTES
    STATUS_SIZE = args.status_bytes
    if args.client:
        host = args.host or DEFAULT_CLIENT_HOST
        oneshot = (
            args.set_spec is not None
            or args.poll
            or args.panel is not None
            or args.held is not None
            or args.suspect_spec is not None
            or args.clear_alarms
            or args.status
        )
        try:
            return run_client(
                host,
                args.port,
                args.set_spec,
                oneshot,
                panel=args.panel,
                held=args.held,
                suspect_spec=args.suspect_spec,
                clear_alarms=args.clear_alarms,
                status_poll=args.status,
            )
        except (OSError, ValueError) as exc:
            print(f"[client] error: {exc}", file=sys.stderr)
            return 1

    host = args.host or DEFAULT_HOST
    try:
        run_server(host, args.port)
    except KeyboardInterrupt:
        print("\n[plc] stopped", flush=True)
    except OSError as exc:
        print(f"[plc] failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
