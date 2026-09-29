#!/usr/bin/env python3
"""Отправить ПЛК команду 203 и напечатать ответ целиком.

Пульт шлёт 4 байта [203, 0, 0, 0] и читает, сколько ПЛК отдаст.
Скрипт делает то же самое и не обрезает пакет до фиксированной длины.

  python3 line-hmi-qt/scripts/plc_203.py
  python3 line-hmi-qt/scripts/plc_203.py --host 192.168.58.88 --port 2025
"""

from __future__ import annotations

import argparse
import socket
import sys

DEFAULT_HOST = "192.168.58.88"
DEFAULT_PORT = 2025
COMMAND = bytes((203, 0, 0, 0))


def recv_all(sock: socket.socket) -> bytes:
    chunks: list[bytes] = []
    while True:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        chunks.append(chunk)
    return b"".join(chunks)


def print_packet(buf: bytes) -> None:
    print(f"bytes: {len(buf)}")
    print(f"hex:   {buf.hex() or '-'}")
    if not buf:
        return
    print("uint8:")
    for start in range(0, len(buf), 10):
        row = buf[start : start + 10]
        cells = " ".join(f"{b:3d}" for b in row)
        print(f"  {start:02d}  {cells}")
    print(f"делится на int16: {len(buf) % 2 == 0}  ({len(buf) // 2} значений)")
    print(f"делится на int32: {len(buf) % 4 == 0}  ({len(buf) // 4} значений, остаток {len(buf) % 4})")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Отправить ПЛК [203,0,0,0] и напечатать ответ")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--timeout", type=float, default=1.2, help="сколько ждать после последнего байта, с")
    args = parser.parse_args(argv)

    print(f"-> {args.host}:{args.port}  {list(COMMAND)}")
    try:
        sock = socket.create_connection((args.host, args.port), timeout=3)
    except OSError as exc:
        print(f"нет соединения: {exc}", file=sys.stderr)
        return 1

    try:
        sock.settimeout(args.timeout)
        sock.sendall(COMMAND)
        packet = recv_all(sock)
    except OSError as exc:
        print(f"ошибка обмена: {exc}", file=sys.stderr)
        return 1
    finally:
        sock.close()

    print_packet(packet)
    return 0 if packet else 1


if __name__ == "__main__":
    raise SystemExit(main())
