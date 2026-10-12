#!/usr/bin/env python3
"""Эмулятор ПЛК по протоколу пульт ↔ ПЛК v2 (docs/PLC_PROTOCOL.md).

Это эталонная реализация логики ПЛК для проверки пульта без железа:
режимы линии, проверки старта, предподготовка, счётчики, авария, сторожевой таймер.

Запуск сервера:
  python3 scripts/plc_emulator.py [--host 0.0.0.0] [--port 2025] [--robot-port 2021] [--hmi-ip 127.0.0.1]

--robot-port: порт для robot-behaviour (plc.port в behaviourConfig.json). На нём опрос 203 —
4 байта, а tcp_send_comand из скриптов — пакет выходов K1..K12 (1024 байта, 255 — не менять).

--status-bytes: размер кадра в ответ на 203, по умолчанию 160. Ставьте как «Кадр, байт» в
админке пульта; ячейки 0..79 заполняются всегда, остальное — нули или подмены (set).

--hmi-ip: с какого IP опрос 203 читается как 6-байтный опрос пульта. Без флага пультом
считается любое соединение (удобно, если Behaviour к эмулятору не подключается).

Консоль сервера (TTY) и клиент из второго терминала понимают одни и те же действия:
  estop                — нажали физическую кнопку e-stop (грибок зажат)
  mushroom 0|1         — грибок: 0 = зажат, 1 = отжат
  doors 0|1            — двери: 0 = открыты, 1 = закрыты (в одной цепи с грибком, ячейка 51)
  nolamel 0|1          — долго нет ламелей (1 — авария)
  noholders 0|1        — долго нет держателей ламелей (1 — авария)
  lameltype T          — на выдаче ламель типа T (0 — пусто)
  start | stop         — кнопки СТАРТ / СТОП на корпусе
  set I V | unset I|all — подменить ячейку кадра int16[I] значением V / вернуть как было
  reboot [estop]       — перезагрузка ПЛК; estop — включение после аварии
  ready NAME 0|1       — готовность: weld (клапаны сварки в безопасном положении) vision1 vision2
  holders big|small 0|1 — датчик наличия больших (int16[25]) / малых (int16[1]) держателей
  status | help | quit

  python3 scripts/plc_emulator.py --client --do "mushroom 1"
  python3 scripts/plc_emulator.py --client --status

Веб-страница управления: http://127.0.0.1:8025 (--web PORT, --web 0 — выключить).
"""

from __future__ import annotations

import argparse
import collections
import shlex
import socket
import sys
import threading
import time

STATUS_INT_COUNT = 80
STATUS_SIZE = STATUS_INT_COUNT * 2

POLL_TAG = 203
CMD_TAG = 7
SERVICE_TAG = 99
DO_TAG = 2
DEBUG_TAG = 204

HMI_POLL_SIZE = 6
OTHER_POLL_SIZE = 4
DO_COUNT = 50
ROBOT_DO_PACKET_SIZE = 1024
ROBOT_DO_COUNT = 12
ROBOT_DO_HOLD = 255

# Ячейки кадра (int16).
WELD_VALVE_INTS = (10, 11, 12, 13, 14, 16)
WELD_VALVE_SAFE = 1  # безопасное положение клапана сварки
SMALL_HOLDERS_SENSOR_INT = 1
BIG_HOLDERS_SENSOR_INT = 25
PANEL_INT = 50
MUSHROOM_INT = 51
MODE_INT = 52
PROGRAM_INT = 54
LOAD_CELL_INT = 55
PREP_DONE_INT = 56
VISION1_INT = 57
VISION2_INT = 58
NO_LAMEL_INT = 59
INIT_PERMIT_INT = 60
NO_HOLDERS_INT = 61
GOOD_INT = 62
SCRAP_INT = 63
SAFE_POWER_OFF_INT = 64
LAMEL_TYPE_INT = 69

MODE_BOOT, MODE_READY, MODE_STOPPING, MODE_RUN, MODE_ESTOP, MODE_SERVICE = range(6)
MODE_NAMES = {
    MODE_BOOT: "Boot",
    MODE_READY: "Ready",
    MODE_STOPPING: "Stopping",
    MODE_RUN: "Run",
    MODE_ESTOP: "Estop",
    MODE_SERVICE: "Service",
}
LINE_ACTIVE = (MODE_RUN, MODE_STOPPING)

PANEL_STOP = 10
PANEL_START = 20
PANEL_ESTOP = 30

HMI_TIMEOUT_S = 3.0
BOOT_DELAY_S = 1.0
READY_NAMES = ("weld", "vision1", "vision2")
HOLDER_NAMES = ("big", "small")

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 2025
DEFAULT_ROBOT_PORT = 2021
DEFAULT_CLIENT_HOST = "127.0.0.1"


LOG_TAIL: collections.deque[str] = collections.deque(maxlen=200)


def log(text: str) -> None:
    LOG_TAIL.append(f"{time.strftime('%H:%M:%S')} {text}")
    print(f"[plc] {text}", flush=True)


def put_i16(buf: bytearray, index: int, value: int) -> None:
    v = int(value) & 0xFFFF
    buf[index * 2] = v & 0xFF
    buf[index * 2 + 1] = (v >> 8) & 0xFF


def get_i16(buf: bytes, index: int) -> int:
    return int.from_bytes(buf[index * 2 : index * 2 + 2], "little", signed=True)


class Plc:
    """Состояние и решения ПЛК. Все методы вызываются под self.lock."""

    def __init__(self, boot_estop: bool) -> None:
        self.lock = threading.Lock()
        self.mode = MODE_BOOT
        self.boot_at = time.monotonic()
        self.boot_estop = boot_estop

        self.prep_done = False
        self.program = 0
        self.load_cell = 0
        self.no_lamel = False
        self.no_holders = False
        self.lamel_type = 1
        self.holders = {name: True for name in HOLDER_NAMES}
        self.init_permit = 0
        self.good = 0
        self.scrap = 0
        self.panel = 0
        self.mushroom = 1
        self.doors_closed = True
        self.ready = {name: True for name in READY_NAMES}

        self.init_done = False

        self.hmi_flags = (0, 0, 0, 0)
        self.zone = 0
        self.hmi_last_poll = 0.0
        self.hmi_lost = False

        self.service_do = [0] * DO_COUNT
        self.overrides: dict[int, int] = {}
        self.robot_do = [0] * ROBOT_DO_COUNT

    # --- режимы -------------------------------------------------------------

    def set_mode(self, mode: int, why: str) -> None:
        if mode == self.mode:
            return
        log(f"mode {MODE_NAMES[self.mode]} → {MODE_NAMES[mode]} ({why})")
        self.mode = mode

    def robot_ready(self) -> bool:
        nats, ctrl, behaviour, _running = self.hmi_flags
        return bool(nats and ctrl and behaviour)

    def alarm_active(self) -> str:
        """Физическая причина аварии, которая ещё не снята; пустая строка — нет."""
        if not self.estop_circuit_ok():
            return "грибок зажат или двери открыты"
        if self.no_lamel:
            return "долго нет ламелей"
        if self.no_holders:
            return "долго нет держателей"
        return ""

    def estop_circuit_ok(self) -> bool:
        return bool(self.mushroom and self.doors_closed)

    def sensor(self, index: int, value: int) -> int:
        """Значение датчика так, как его видит ПЛК: ручная подмена ячейки важнее."""
        return self.overrides.get(index, value)

    def prep_waiting(self) -> list[str]:
        """Что ещё не готово, чтобы закончить предподготовку (56 = 1)."""
        waiting = []
        if self.sensor(LAMEL_TYPE_INT, self.lamel_type) == 0:
            waiting.append("ламель на выдаче")
        if self.program == 1:
            holders = self.sensor(BIG_HOLDERS_SENSOR_INT, int(self.holders["big"]))
        else:
            holders = self.sensor(SMALL_HOLDERS_SENSOR_INT, int(self.holders["small"]))
        if holders != 1:
            waiting.append("держатели")
        valves_closed = all(self.sensor(i, WELD_VALVE_SAFE) == WELD_VALVE_SAFE for i in WELD_VALVE_INTS)
        if not self.ready["weld"] or not valves_closed:
            waiting.append("клапаны сварки")
        if not self.init_done:
            waiting.append("init")
        return waiting

    def raise_alarm(self, why: str) -> None:
        if self.mode not in (MODE_ESTOP, MODE_SERVICE, MODE_BOOT):
            self.enter_estop(why)

    def leave_line(self) -> None:
        """Линия уходит из «в работе» / «остановки»: незаконченный цикл — брак."""
        if self.mode in LINE_ACTIVE and self.prep_done:
            self.scrap += 1
            log(f"цикл прерван, брак = {self.scrap}")
        self.prep_done = False

    def enter_estop(self, why: str) -> None:
        self.leave_line()
        self.panel = PANEL_ESTOP
        self.set_mode(MODE_ESTOP, why)
        log("ПЛК: stop роботу в NATS (в эмуляторе не отправляется)")

    def request_start(self, source: str) -> None:
        if self.mode != MODE_READY:
            log(f"{source}: старт отклонён, режим {MODE_NAMES[self.mode]}")
            return
        if self.program == 0:
            log(f"{source}: старт отклонён, программа не выбрана")
            return
        if self.zone == 0:
            log(f"{source}: старт отклонён, робот не в стартовой зоне")
            return
        if self.panel == PANEL_START:
            self.panel = 0
        self.init_done = False
        self.prep_done = False
        self.init_permit = (self.init_permit + 1) & 0x7FFF
        self.set_mode(MODE_RUN, f"{source}: предподготовка, разрешение init = {self.init_permit}")

    def request_stop(self, source: str) -> None:
        if self.mode != MODE_RUN:
            log(f"{source}: стоп проигнорирован, режим {MODE_NAMES[self.mode]}")
            return
        self.set_mode(MODE_STOPPING, f"{source}: ждём 7 8")

    # --- команды пульта -----------------------------------------------------

    def on_command(self, sub: int, a: int, b: int) -> None:
        if sub == 1:
            self.on_select(a, b)
        elif sub == 2:
            self.request_start("7 2")
        elif sub == 3:
            self.request_stop("7 3")
        elif sub == 5:
            self.on_estop_exit()
        elif sub == 7:
            self.on_script_report(a, b)
        elif sub == 8:
            self.on_stopped()
        else:
            log(f"неизвестная команда 7 {sub} {a} {b}")

    def on_select(self, program: int, load_cell: int) -> None:
        if program not in (1, 2) or load_cell not in (0, 1):
            log(f"7 1 {program} {load_cell}: неверные параметры")
            return
        if self.mode not in (MODE_READY, MODE_BOOT):
            log(f"7 1: выбор программы отклонён, режим {MODE_NAMES[self.mode]}")
            return
        if program != self.program:
            self.good = 0
            self.scrap = 0
            log(f"программа {self.program} → {program}, счётчики обнулены")
        self.program = program
        self.load_cell = load_cell
        log(f"7 1: программа {program}, тензодатчик {'вкл' if load_cell else 'выкл'}")

    def on_estop_exit(self) -> None:
        if self.mode != MODE_ESTOP:
            log("7 5: не в аварии, игнор")
            return
        why = self.alarm_active()
        if why:
            log(f"7 5: выход из аварии отклонён, {why}")
            return
        if self.panel == PANEL_ESTOP:
            self.panel = 0
        self.set_mode(MODE_READY, "7 5: авария снята")

    def on_script_report(self, role: int, ok: int) -> None:
        log(f"7 7 {role} {ok}: {'init' if role == 1 else 'основной'} {'дошёл' if ok else 'упал'}")
        if self.mode not in LINE_ACTIVE:
            return
        stopping = self.mode == MODE_STOPPING
        if role == 1:
            if self.prep_done:
                return
            if ok:
                self.init_done = True
            elif not stopping:
                self.enter_estop("init не выполнился")
        elif role == 2:
            if not self.prep_done:
                return
            if ok:
                self.good += 1
                log(f"годные = {self.good}")
            elif not stopping:
                self.enter_estop("основной скрипт упал")

    def on_stopped(self) -> None:
        if self.mode != MODE_STOPPING:
            log("7 8: не в остановке, игнор")
            return
        self.prep_done = False
        if self.panel == PANEL_STOP:
            self.panel = 0
        self.set_mode(MODE_READY, "7 8: стоп отработан")

    def on_service(self, on: bool) -> None:
        if on and self.mode == MODE_READY:
            self.set_mode(MODE_SERVICE, "99 1")
        elif not on and self.mode == MODE_SERVICE:
            self.service_do = [0] * DO_COUNT
            self.set_mode(MODE_READY, "99 0")
        else:
            log(f"99 {int(on)}: игнор в режиме {MODE_NAMES[self.mode]}")

    def on_do(self, index: int, value: int) -> None:
        if self.mode != MODE_SERVICE or not 0 <= index < DO_COUNT or value not in (0, 1):
            log(f"2 {index} {value}: игнор")
            return
        self.service_do[index] = value

    def on_robot_do(self, packet: bytes) -> None:
        changed = []
        for i, value in enumerate(packet[:ROBOT_DO_COUNT]):
            if value != ROBOT_DO_HOLD and value != self.robot_do[i]:
                self.robot_do[i] = value
                changed.append(f"K{i + 1}={value}")
        log(f"выходы от робота: {', '.join(changed) if changed else 'без изменений'}")

    def on_hmi_poll(self, flags: tuple[int, int, int, int], zone: int) -> None:
        if self.hmi_flags != flags or self.zone != zone:
            log(f"203: nats={flags[0]} ctrl={flags[1]} behaviour={flags[2]} script={flags[3]} zone={zone}")
        if self.hmi_lost:
            self.hmi_lost = False
            log("пульт снова на связи")
        self.hmi_flags = flags
        self.zone = zone
        self.hmi_last_poll = time.monotonic()

    # --- время --------------------------------------------------------------

    def tick(self) -> None:
        now = time.monotonic()

        if self.mode == MODE_BOOT and now - self.boot_at >= BOOT_DELAY_S and self.hmi_last_poll:
            if self.boot_estop:
                self.enter_estop("авария сохранена с прошлого включения")
            else:
                self.set_mode(MODE_READY, "включение")

        hmi_alive = self.hmi_last_poll and now - self.hmi_last_poll < HMI_TIMEOUT_S
        if self.hmi_last_poll and not hmi_alive and not self.hmi_lost:
            self.hmi_lost = True
            log("нет опроса от пульта 3 с")
            if self.mode in LINE_ACTIVE:
                self.leave_line()
                self.set_mode(MODE_READY, "пульт пропал, цикл прерван")

        if self.mode == MODE_RUN and not self.prep_done and not self.prep_waiting():
            self.prep_done = True
            log("предподготовка закончена (56 = 1)")

    # --- кадр ---------------------------------------------------------------

    def build_status(self) -> bytes:
        # Ячейки 0..79 заполняются всегда; ответ обрезается или дополняется нулями до STATUS_SIZE.
        out = bytearray(max(STATUS_SIZE, 160))
        if self.mode == MODE_SERVICE:
            for i, v in enumerate(self.service_do):
                put_i16(out, i, v)
        else:
            valves_open = (self.mode in LINE_ACTIVE and self.prep_done) or not self.ready["weld"]
            for index in WELD_VALVE_INTS:
                put_i16(out, index, 0 if valves_open else WELD_VALVE_SAFE)
            put_i16(out, BIG_HOLDERS_SENSOR_INT, int(self.holders["big"]))
            put_i16(out, SMALL_HOLDERS_SENSOR_INT, int(self.holders["small"]))
        put_i16(out, PANEL_INT, self.panel)
        put_i16(out, MUSHROOM_INT, int(self.estop_circuit_ok()))
        put_i16(out, MODE_INT, self.mode)
        put_i16(out, PROGRAM_INT, self.program)
        put_i16(out, LOAD_CELL_INT, self.load_cell)
        put_i16(out, PREP_DONE_INT, int(self.prep_done))
        put_i16(out, VISION1_INT, int(self.ready["vision1"]))
        put_i16(out, VISION2_INT, int(self.ready["vision2"]))
        put_i16(out, NO_LAMEL_INT, int(self.no_lamel))
        put_i16(out, INIT_PERMIT_INT, self.init_permit)
        put_i16(out, NO_HOLDERS_INT, int(self.no_holders))
        put_i16(out, GOOD_INT, self.good)
        put_i16(out, SCRAP_INT, self.scrap)
        put_i16(out, SAFE_POWER_OFF_INT, int(self.mode not in LINE_ACTIVE))
        put_i16(out, LAMEL_TYPE_INT, self.lamel_type)
        for index, value in self.overrides.items():
            put_i16(out, index, value)
        return bytes(out[:STATUS_SIZE])

    # --- физические входы (консоль) -----------------------------------------

    def action(self, line: str) -> str:
        parts = shlex.split(line)
        if not parts:
            return ""
        cmd, args = parts[0], parts[1:]
        flag = args[0] == "1" if args and args[0] in ("0", "1") else None
        if cmd == "estop" or (cmd == "mushroom" and flag is False):
            self.mushroom = 0
            self.raise_alarm("кнопка e-stop")
            return "e-stop нажат"
        if cmd == "mushroom" and flag:
            self.mushroom = 1
            return "грибок отжат"
        if cmd == "doors" and flag is not None:
            self.doors_closed = flag
            if not flag:
                self.raise_alarm("двери открыты")
            return f"двери {'закрыты' if flag else 'открыты'}"
        if cmd == "nolamel" and flag is not None:
            self.no_lamel = flag
            if flag:
                self.raise_alarm("долго нет ламелей")
            return f"долго нет ламелей = {int(flag)}"
        if cmd == "noholders" and flag is not None:
            self.no_holders = flag
            if flag:
                self.raise_alarm("долго нет держателей")
            return f"долго нет держателей = {int(flag)}"
        if cmd == "lameltype" and args and args[0].isdigit():
            self.lamel_type = int(args[0])
            return f"на выдаче тип {self.lamel_type}"
        if cmd == "start":
            self.panel = PANEL_START
            self.request_start("кнопка СТАРТ")
            return "кнопка СТАРТ"
        if cmd == "stop":
            if self.mode == MODE_RUN:
                self.panel = PANEL_STOP
            self.request_stop("кнопка СТОП")
            return "кнопка СТОП"
        if cmd == "ready" and len(args) == 2 and args[0] in READY_NAMES and args[1] in ("0", "1"):
            self.ready[args[0]] = args[1] == "1"
            return f"{args[0]} = {args[1]}"
        if cmd == "holders" and len(args) == 2 and args[0] in HOLDER_NAMES and args[1] in ("0", "1"):
            self.holders[args[0]] = args[1] == "1"
            return f"держатели {args[0]} = {args[1]}"
        if cmd == "set" and len(args) == 2:
            try:
                index, value = int(args[0]), int(args[1])
            except ValueError:
                return "set I V: числа"
            if not 0 <= index < STATUS_INT_COUNT or not -32768 <= value <= 32767:
                return "set I V: I 0..79, V int16"
            self.overrides[index] = value
            return f"int16[{index}] = {value} (подмена)"
        if cmd == "unset" and args:
            if args[0] == "all":
                self.overrides.clear()
                return "подмены сняты"
            if args[0].isdigit():
                self.overrides.pop(int(args[0]), None)
                return f"int16[{args[0]}] без подмены"
        if cmd == "reboot":
            self.boot_estop = args[:1] == ["estop"]
            self.prep_done = False
            self.init_done = False
            self.panel = 0
            self.boot_at = time.monotonic()
            self.set_mode(MODE_BOOT, "перезагрузка" + (" после аварии" if self.boot_estop else ""))
            return "ПЛК перезагружается"
        if cmd == "status":
            return self.describe()
        return "неизвестно; help"

    def describe(self) -> str:
        return (
            f"mode={MODE_NAMES[self.mode]} prep_done={int(self.prep_done)} program={self.program} "
            f"loadcell={self.load_cell} init={self.init_permit} "
            f"good={self.good} scrap={self.scrap} "
            f"prep_waiting={self.prep_waiting()} holders={self.holders} "
            f"mushroom={self.mushroom} doors={'closed' if self.doors_closed else 'open'} "
            f"nolamel={int(self.no_lamel)} noholders={int(self.no_holders)} "
            f"lameltype={self.lamel_type} "
            f"zone={self.zone} flags={self.hmi_flags} hmi_lost={int(self.hmi_lost)}"
        )


HELP = __doc__.split("Консоль сервера", 1)[1].split("python3", 1)[0]


def handle_client(conn: socket.socket, addr: tuple[str, int], plc: Plc, hmi_ip: str | None,
                  robot: bool = False) -> None:
    peer = f"{addr[0]}:{addr[1]}"
    is_hmi = not robot and (hmi_ip is None or addr[0] == hmi_ip)
    log(f"подключился {peer} ({'пульт' if is_hmi else 'робот' if robot else 'другой клиент'})")
    try:
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass
    pending = bytearray()
    try:
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                log(f"отключился {peer}")
                return
            pending.extend(chunk)
            while pending:
                tag = pending[0]
                if tag == POLL_TAG:
                    size = HMI_POLL_SIZE if is_hmi else OTHER_POLL_SIZE
                elif tag in (CMD_TAG, DEBUG_TAG):
                    size = 4
                elif tag == SERVICE_TAG:
                    size = 2
                elif tag == DO_TAG:
                    size = 3
                elif robot:
                    tag = None
                    size = ROBOT_DO_PACKET_SIZE
                else:
                    log(f"{peer}: неизвестный байт {tag}, пропущен")
                    del pending[:1]
                    continue
                if len(pending) < size:
                    break
                frame = bytes(pending[:size])
                del pending[:size]
                reply = None
                with plc.lock:
                    if tag == POLL_TAG:
                        if is_hmi:
                            plc.on_hmi_poll(tuple(1 if b else 0 for b in frame[1:5]), frame[5])
                        reply = plc.build_status()
                    elif tag == CMD_TAG:
                        plc.on_command(frame[1], frame[2], frame[3])
                    elif tag == SERVICE_TAG:
                        plc.on_service(frame[1] != 0)
                    elif tag == DO_TAG:
                        plc.on_do(frame[1], frame[2])
                    elif tag is None:
                        plc.on_robot_do(frame)
                if tag == DEBUG_TAG:
                    is_hmi = False
                    length = frame[1]
                    text = bytes(pending[:length]).decode("utf-8", "replace")
                    del pending[:length]
                    with plc.lock:
                        result = plc.action(text)
                    log(f"{peer}: {text} → {result}")
                    conn.sendall(result.encode("utf-8") + b"\n")
                if reply is not None:
                    conn.sendall(reply)
    except OSError as exc:
        log(f"{peer}: {exc}")
    finally:
        conn.close()


def run_console(plc: Plc, stop: threading.Event) -> None:
    if not sys.stdin.isatty():
        return
    log("консоль: help — список действий")
    while not stop.is_set():
        try:
            line = input().strip()
        except (EOFError, KeyboardInterrupt):
            stop.set()
            return
        if line in ("q", "quit", "exit"):
            stop.set()
            return
        if line in ("h", "help", "?"):
            print(HELP, flush=True)
            continue
        with plc.lock:
            result = plc.action(line)
        if result:
            log(result)


def run_ticker(plc: Plc, stop: threading.Event) -> None:
    while not stop.is_set():
        with plc.lock:
            plc.tick()
        time.sleep(0.1)


def serve(srv: socket.socket, plc: Plc, hmi_ip: str | None, robot: bool, stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            conn, addr = srv.accept()
        except TimeoutError:
            continue
        threading.Thread(target=handle_client, args=(conn, addr, plc, hmi_ip, robot), daemon=True).start()


def listen(host: str, port: int) -> socket.socket:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, port))
    srv.listen(8)
    srv.settimeout(0.5)
    return srv


def run_server(host: str, port: int, robot_port: int, hmi_ip: str | None, boot_estop: bool,
               web_host: str, web_port: int) -> None:
    plc = Plc(boot_estop)
    stop = threading.Event()
    if web_port:
        import plc_web

        plc_web.start(plc, sys.modules[__name__], web_host, web_port)
        log(f"веб: http://{web_host}:{web_port}")
    with listen(host, port) as srv:
        log(f"слушаю {host}:{port}, кадр {STATUS_SIZE} байт, пульт: {hmi_ip or 'любое соединение'}")
        robot_srv = None
        if robot_port:
            robot_srv = listen(host, robot_port)
            log(f"слушаю {host}:{robot_port} для robot-behaviour")
            threading.Thread(target=serve, args=(robot_srv, plc, hmi_ip, True, stop), daemon=True).start()
        threading.Thread(target=run_console, args=(plc, stop), daemon=True).start()
        threading.Thread(target=run_ticker, args=(plc, stop), daemon=True).start()
        try:
            serve(srv, plc, hmi_ip, False, stop)
        finally:
            if robot_srv is not None:
                robot_srv.close()
    log("остановлен")


def print_status(frame: bytes) -> None:
    print(
        f"mode={MODE_NAMES.get(get_i16(frame, MODE_INT), '?')} "
        f"prep_done={get_i16(frame, PREP_DONE_INT)} program={get_i16(frame, PROGRAM_INT)} "
        f"loadcell={get_i16(frame, LOAD_CELL_INT)} "
        f"nolamel={get_i16(frame, NO_LAMEL_INT)} noholders={get_i16(frame, NO_HOLDERS_INT)} "
        f"lameltype={get_i16(frame, LAMEL_TYPE_INT)} "
        f"init={get_i16(frame, INIT_PERMIT_INT)} "
        f"good={get_i16(frame, GOOD_INT)} scrap={get_i16(frame, SCRAP_INT)} "
        f"poweroff={get_i16(frame, SAFE_POWER_OFF_INT)} "
        f"weld_valves={[get_i16(frame, i) for i in WELD_VALVE_INTS]} "
        f"holders_big={get_i16(frame, BIG_HOLDERS_SENSOR_INT)} holders_small={get_i16(frame, SMALL_HOLDERS_SENSOR_INT)} "
        f"panel={get_i16(frame, PANEL_INT)} mushroom={get_i16(frame, MUSHROOM_INT)}"
    )


def run_client(host: str, port: int, action: str | None, status: bool) -> int:
    with socket.create_connection((host, port), timeout=5.0) as sock:
        # Отладочная команда первой: после неё эмулятор не считает соединение пультом.
        data = (action or "status").encode("utf-8")
        sock.sendall(bytes((DEBUG_TAG, len(data), 0, 0)) + data)
        reader = sock.makefile("rb")
        print(reader.readline().decode("utf-8").rstrip())
        if status:
            sock.sendall(bytes((POLL_TAG, 0, 0, 0)))
            print_status(reader.read(STATUS_SIZE))
    return 0


def main(argv: list[str] | None = None) -> int:
    global STATUS_INT_COUNT, STATUS_SIZE
    p = argparse.ArgumentParser(description="Эмулятор ПЛК, протокол v2")
    p.add_argument("--host", default=None)
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--robot-port", type=int, default=DEFAULT_ROBOT_PORT,
                   help="порт для robot-behaviour, 0 — не слушать")
    p.add_argument("--hmi-ip", default=None, help="IP пульта; без флага пульт — любое соединение")
    p.add_argument("--boot-estop", action="store_true", help="авария сохранена с прошлого включения")
    p.add_argument("--status-bytes", type=int, default=STATUS_SIZE,
                   help="размер кадра в байтах, чётный (как «Кадр, байт» в админке пульта)")
    p.add_argument("--web", type=int, default=8025, help="порт веб-страницы, 0 — без неё")
    p.add_argument("--web-host", default="127.0.0.1", help="адрес веб-страницы (0.0.0.0 — для всей сети)")
    p.add_argument("--client", action="store_true", help="клиент: отправить действие или прочитать кадр")
    p.add_argument("--do", dest="action", default=None, help='клиент: действие, например "mushroom 1"')
    p.add_argument("--status", action="store_true", help="клиент: прочитать кадр (как другой клиент, 4 байта)")
    args = p.parse_args(argv)
    if args.status_bytes < 2 or args.status_bytes % 2:
        p.error("--status-bytes: нужно чётное число ≥ 2")
    STATUS_SIZE = args.status_bytes
    STATUS_INT_COUNT = STATUS_SIZE // 2

    if args.client:
        try:
            return run_client(args.host or DEFAULT_CLIENT_HOST, args.port, args.action, args.status)
        except OSError as exc:
            print(f"ошибка: {exc}", file=sys.stderr)
            return 1

    try:
        run_server(args.host or DEFAULT_HOST, args.port, args.robot_port, args.hmi_ip, args.boot_estop,
                   args.web_host, args.web)
    except KeyboardInterrupt:
        log("остановлен")
    except OSError as exc:
        print(f"ошибка: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
