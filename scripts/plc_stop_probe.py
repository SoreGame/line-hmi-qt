#!/usr/bin/env python3
"""Зонд: почему ПЛК в Stop/State=4 при старте.

Шлёт опрос 203 (6 байт, как пульт) на реальный ПЛК и пишет NDJSON в debug-лог.
Смотрит ячейки кадра, которые в KUZBASSFINAL_v0.4 заполняет _STB72:

  [50] BTN_PRESSED  (10=IDLE/стоп, 20=старт, 30=e-stop)
  [51] BTN_EMERGENCY (0 = цепь разомкнута → в MAIN: LDI → Stop:=TRUE)
  [52] State         (1=готов, 3=работа, 4=авария из _STB88 при Stop)

  python3 scripts/plc_stop_probe.py
  python3 scripts/plc_stop_probe.py --host 192.168.58.88 --seconds 15
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import time
from pathlib import Path

LOG_PATH = Path("/home/egor/repos/robot/.cursor/debug-3a8ff8.log")
SESSION_ID = "3a8ff8"
POLL = bytes((203, 1, 1, 1, 0, 1))  # f1..f4,Z — как у пульта


def log(hypothesis_id: str, location: str, message: str, data: dict) -> None:
    # #region agent log
    payload = {
        "sessionId": SESSION_ID,
        "runId": "plc-stop-probe",
        "hypothesisId": hypothesis_id,
        "location": location,
        "message": message,
        "data": data,
        "timestamp": int(time.time() * 1000),
    }
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    with LOG_PATH.open("a", encoding="utf-8") as f:
        f.write(json.dumps(payload, ensure_ascii=False) + "\n")
    # #endregion


def recv_frame(sock: socket.socket, expect: int, timeout: float) -> bytes:
    sock.settimeout(timeout)
    buf = bytearray()
    deadline = time.monotonic() + timeout
    while len(buf) < expect and time.monotonic() < deadline:
        try:
            chunk = sock.recv(expect - len(buf))
        except socket.timeout:
            break
        if not chunk:
            break
        buf.extend(chunk)
    return bytes(buf)


def cells(buf: bytes) -> list[int]:
    n = len(buf) // 2
    return list(struct.unpack("<" + "h" * n, buf[: n * 2]))


def main() -> int:
    p = argparse.ArgumentParser(description="Probe PLC Stop/State on real hardware")
    p.add_argument("--host", default="192.168.58.88")
    p.add_argument("--port", type=int, default=2025)
    p.add_argument("--seconds", type=float, default=12.0)
    p.add_argument("--status-bytes", type=int, default=160)
    args = p.parse_args()

    log(
        "A",
        "plc_stop_probe.py:main",
        "probe_start",
        {"host": args.host, "port": args.port, "seconds": args.seconds},
    )
    print(f"-> {args.host}:{args.port}  poll={list(POLL)}  {args.seconds}s")

    try:
        sock = socket.create_connection((args.host, args.port), timeout=3)
    except OSError as exc:
        log("A", "plc_stop_probe.py:connect", "connect_failed", {"error": str(exc)})
        print(f"нет соединения: {exc}")
        return 1

    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    t0 = time.monotonic()
    n = 0
    prev: tuple | None = None
    try:
        while time.monotonic() - t0 < args.seconds:
            # сброс хвоста прошлого кадра
            sock.settimeout(0.05)
            try:
                while sock.recv(4096):
                    pass
            except socket.timeout:
                pass

            sock.sendall(POLL)
            frame = recv_frame(sock, args.status_bytes, 0.9)
            if len(frame) < args.status_bytes:
                log(
                    "A",
                    "plc_stop_probe.py:poll",
                    "short_frame",
                    {"got": len(frame), "want": args.status_bytes},
                )
                print(f"короткий кадр: {len(frame)} байт")
                time.sleep(0.5)
                continue

            v = cells(frame)
            btn_pressed = v[50] if len(v) > 50 else None
            emergency = v[51] if len(v) > 51 else None
            state = v[52] if len(v) > 52 else None
            program = v[54] if len(v) > 54 else None
            # гипотеза A: BTN_EMERGENCY=0 → Stop → State=4
            # гипотеза B: State=4 при emergency=1 (Stop залип / не сброшен 7 5)
            # гипотеза C: BTN_PRESSED=30 (e-stop) при старте
            # гипотеза D: State=2 (Stopping) — в этом проекте MAIN не пишет State:=2
            hyp = "A" if emergency == 0 else ("B" if state == 4 else ("D" if state == 2 else "C"))
            snap = (btn_pressed, emergency, state, program)
            data = {
                "n": n,
                "btn_pressed_50": btn_pressed,
                "btn_emergency_51": emergency,
                "state_52": state,
                "program_54": program,
                "changed": snap != prev,
            }
            log(hyp, "plc_stop_probe.py:poll", "frame", data)
            if snap != prev or n == 0:
                print(
                    f"t={time.monotonic()-t0:5.1f}s  "
                    f"State={state}  BTN_EMERGENCY={emergency}  "
                    f"BTN_PRESSED={btn_pressed}  prog={program}"
                )
            prev = snap
            n += 1
            time.sleep(0.5)
    finally:
        sock.close()

    log("A", "plc_stop_probe.py:main", "probe_done", {"frames": n})
    print(f"готово, кадров: {n}. лог: {LOG_PATH}")
    return 0 if n else 1


if __name__ == "__main__":
    raise SystemExit(main())
