#!/usr/bin/env python3
"""PLC emulator over raw TCP — fixed 1024-byte frames.

Memory map (one byte = one signal, 0=off / 1=on):

  Offset   Size   Name            Direction
  0..11    12     VALVE K1..K12   client → PLC (command); PLC echoes actual state
  12..23   12     SENSOR S1..S12  PLC → client only (client bytes ignored)
  24..1023 1000   RESERVED        always 0

Exchange: client sends 1024 bytes → PLC applies VALVE[0..11], updates sensors,
optionally replies with 1024 bytes (valve echo + sensors + zeros).

Valve bytes other than 0/1 are ignored (client may send 0xFF to hold state on poll).

Multi-client: HMI (reads replies) and Behaviour (write-only tcp_send_comand) can
share one PLC. If a client never reads the reply, the emulator skips the reply
instead of closing the socket (avoids Broken pipe in ChaiScript).

Usage:
  python3 plc_emulator.py [--host 0.0.0.0] [--port 1502]
  python3 plc_emulator.py --client [--host 127.0.0.1] [--port 1502]
  python3 plc_emulator.py --client --set 1=1,3=0
  python3 plc_emulator.py --client --poll
"""

from __future__ import annotations

import argparse
import socket
import sys
import threading
from typing import Iterable

FRAME_SIZE = 1024
VALVE_COUNT = 12
SENSOR_COUNT = 12
VALVE_OFFSET = 0
SENSOR_OFFSET = 12

VALVE_NAMES = [f"K{i}" for i in range(1, VALVE_COUNT + 1)]
SENSOR_NAMES = [f"S{i}" for i in range(1, SENSOR_COUNT + 1)]

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 1502
DEFAULT_CLIENT_HOST = "127.0.0.1"
HOLD_BYTE = 0xFF
REPLY_TIMEOUT_S = 0.25


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


class PlcState:
    """In-memory valve/sensor state shared by all TCP clients."""

    def __init__(self) -> None:
        self.valves = [0] * VALVE_COUNT
        self.sensors = [0] * SENSOR_COUNT
        self._lock = threading.Lock()

    def apply_valve_commands(self, frame: bytes) -> list[tuple[str, int, int]]:
        """Apply bytes[0:12] as valve commands. Returns list of (name, old, new)."""
        changes: list[tuple[str, int, int]] = []
        with self._lock:
            for i in range(VALVE_COUNT):
                raw = frame[VALVE_OFFSET + i]
                if raw not in (0, 1):
                    continue
                old = self.valves[i]
                if old != raw:
                    self.valves[i] = raw
                    changes.append((VALVE_NAMES[i], old, raw))
            self._update_sensors_locked()
        return changes

    def _update_sensors_locked(self) -> None:
        """S_i follows K_i so valve commands are visible in the sensor reply."""
        for i in range(SENSOR_COUNT):
            self.sensors[i] = self.valves[i]

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


def handle_client(conn: socket.socket, addr: tuple[str, int], state: PlcState) -> None:
    peer = f"{addr[0]}:{addr[1]}"
    print(f"[plc] connected {peer}", flush=True)
    try:
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass

    try:
        while True:
            # Blocking read of the next command frame. Do not use a short timeout
            # here — idle HMI/Behaviour connections must stay alive.
            frame = recv_exact(conn, FRAME_SIZE)
            if frame is None:
                print(f"[plc] disconnected {peer}", flush=True)
                return

            changes = state.apply_valve_commands(frame)
            reply, valves, sensors = state.build_reply()
            if changes:
                detail = ", ".join(f"{name}:{old}->{new}" for name, old, new in changes)
                print(f"[plc] {peer} valves changed: {detail}", flush=True)
            else:
                print(
                    f"[plc] {peer} OK  {format_bits(VALVE_NAMES, valves)} | "
                    f"{format_bits(SENSOR_NAMES, sensors)}",
                    flush=True,
                )

            # Reply is for status clients (HMI). Write-only Behaviour is fine
            # if this soft-fails — command was already applied.
            try_send_reply(conn, reply, peer)
    except (ConnectionResetError, BrokenPipeError, OSError) as exc:
        print(f"[plc] {peer} error: {exc}", flush=True)
    finally:
        try:
            conn.close()
        except OSError:
            pass


def run_server(host: str, port: int) -> None:
    state = PlcState()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(16)
        print(f"[plc] listening on {host}:{port} (frame={FRAME_SIZE}, multi-client)", flush=True)
        print(
            f"[plc] map: valves@{VALVE_OFFSET}..{VALVE_OFFSET + VALVE_COUNT - 1} "
            f"sensors@{SENSOR_OFFSET}..{SENSOR_OFFSET + SENSOR_COUNT - 1}",
            flush=True,
        )
        print(
            "[plc] write-only clients (Behaviour tcp_send_comand) supported; "
            "unread replies are skipped, socket stays open",
            flush=True,
        )
        while True:
            conn, addr = srv.accept()
            threading.Thread(
                target=handle_client,
                args=(conn, addr, state),
                daemon=True,
                name=f"plc-{addr[0]}:{addr[1]}",
            ).start()


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


def print_state(label: str, valves: list[int], sensors: list[int]) -> None:
    print(f"[client] {label}")
    print(f"  valves  {format_bits(VALVE_NAMES, valves)}")
    print(f"  sensors {format_bits(SENSOR_NAMES, sensors)}")


def run_client(host: str, port: int, set_spec: str | None, oneshot: bool) -> int:
    desired = [0] * VALVE_COUNT

    if oneshot:
        if set_spec:
            current, _ = exchange(host, port, hold=True)
            desired[:] = current
            for i, v in parse_set_spec(set_spec).items():
                desired[i] = v
            valves, sensors = exchange(host, port, desired)
        else:
            valves, sensors = exchange(host, port, hold=True)
        print_state("exchange" if set_spec else "poll", valves, sensors)
        return 0

    print(
        "Commands: poll | set <N>=<0|1>[,...] | quit\n"
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
        print("unknown command; try: poll | set 1=1,2=0 | quit")


def build_arg_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="PLC raw-TCP emulator (1024-byte frames)")
    p.add_argument("--host", default=None, help="bind/connect host")
    p.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"TCP port (default {DEFAULT_PORT})")
    p.add_argument("--client", action="store_true", help="run as test client instead of server")
    p.add_argument("--set", dest="set_spec", default=None, help="client: valve set e.g. 1=1,3=0")
    p.add_argument("--poll", action="store_true", help="client: single exchange then exit")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    if args.client:
        host = args.host or DEFAULT_CLIENT_HOST
        oneshot = args.set_spec is not None or args.poll
        try:
            return run_client(host, args.port, args.set_spec, oneshot)
        except (OSError, ValueError) as exc:
            print(f"[client] error: {exc}", file=sys.stderr)
            return 1

    host = args.host or DEFAULT_HOST
    try:
        run_server(host, args.port)
    except KeyboardInterrupt:
        print("\n[plc] stopped", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
