"""Веб-страница эмулятора ПЛК: входы, сценарии, подмена ячеек кадра, лог.

Запускается из plc_emulator.py (--web PORT). Только стандартная библиотека.
"""

from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from types import ModuleType

# Сценарий — шаги (пауза перед шагом в секундах, действие эмулятора).
SCENARIOS: list[tuple[str, str, list[tuple[float, str]]]] = [
    ("estop_release", "E-STOP, через 5 с отжать грибок", [(0, "estop"), (5, "mushroom 1")]),
    ("doors", "Открыть двери на 5 с", [(0, "doors 0"), (5, "doors 1")]),
    ("lamels_out", "Ламели кончились, через 10 с «долго нет ламелей»",
     [(0, "lameltype 0"), (10, "nolamel 1")]),
    ("lamels_back", "Ламели вернулись", [(0, "nolamel 0"), (0, "lameltype 1")]),
    ("holders_out", "Держатели кончились, через 10 с «долго нет держателей»",
     [(0, "holders big 0"), (0, "holders small 0"), (10, "noholders 1")]),
    ("holders_back", "Держатели вернулись",
     [(0, "noholders 0"), (0, "holders big 1"), (0, "holders small 1")]),
    ("vision_blink", "Vision 1 пропал на 5 с", [(0, "ready vision1 0"), (5, "ready vision1 1")]),
    ("weld_stuck", "Клапаны сварки не в 1 на 5 с", [(0, "ready weld 0"), (5, "ready weld 1")]),
    ("panel_start", "СТАРТ на корпусе", [(0, "start")]),
    ("panel_stop", "СТОП на корпусе", [(0, "stop")]),
    ("reboot", "Перезагрузка ПЛК", [(0, "reboot")]),
    ("reboot_estop", "Включение после E-STOP", [(0, "reboot estop")]),
    ("all_ok", "Всё в норме: входы по умолчанию, подмены сняты",
     [(0, a) for a in ("mushroom 1", "doors 1", "nolamel 0", "noholders 0", "weldfail 0",
                       "holderpick 0", "lamelpick 0", "ready weld 1",
                       "ready vision1 1", "ready vision2 1", "holders big 1", "holders small 1",
                       "lameltype 1", "unset all")]),
]


class ScenarioRunner:
    """Один сценарий за раз; новый отменяет текущий."""

    def __init__(self, plc, emu: ModuleType) -> None:
        self.plc = plc
        self.emu = emu
        self.name: str | None = None
        self._cancel = threading.Event()
        self._guard = threading.Lock()

    def run(self, scenario_id: str) -> str:
        found = next((s for s in SCENARIOS if s[0] == scenario_id), None)
        if found is None:
            return "нет такого сценария"
        self.cancel()
        _, title, steps = found
        cancel = threading.Event()
        with self._guard:
            self._cancel = cancel
            self.name = title
        threading.Thread(target=self._play, args=(title, steps, cancel), daemon=True).start()
        return f"сценарий: {title}"

    def cancel(self) -> None:
        with self._guard:
            self._cancel.set()
            self.name = None

    def _play(self, title: str, steps: list[tuple[float, str]], cancel: threading.Event) -> None:
        self.emu.log(f"веб: сценарий «{title}»")
        for delay, action in steps:
            if cancel.wait(delay):
                self.emu.log(f"веб: сценарий «{title}» отменён")
                return
            with self.plc.lock:
                result = self.plc.action(action)
            self.emu.log(f"веб: {action} → {result}")
        with self._guard:
            if self._cancel is cancel:
                self.name = None


def cell_names(emu: ModuleType) -> dict[int, str]:
    names = {
        emu.SMALL_HOLDERS_SENSOR_INT: "датчик малых держателей",
        emu.BIG_HOLDERS_SENSOR_INT: "датчик больших держателей",
        emu.PANEL_INT: "кнопки корпуса",
        emu.MUSHROOM_INT: "цепь e-stop (грибок+двери)",
        emu.MODE_INT: "режим",
        emu.PROGRAM_INT: "программа",
        emu.LOAD_CELL_INT: "тензодатчик",
        emu.PREP_DONE_INT: "предподготовка закончена",
        emu.VISION1_INT: "Vision 1",
        emu.VISION2_INT: "Vision 2",
        emu.NO_LAMEL_INT: "долго нет ламелей",
        emu.INIT_PERMIT_INT: "разрешение init",
        emu.NO_HOLDERS_INT: "долго нет держателей",
        emu.GOOD_INT: "годные",
        emu.SCRAP_INT: "брак",
        emu.SAFE_POWER_OFF_INT: "можно выключать",
        emu.WELD_TIMEOUT_INT: "сварка долго не отрабатывает",
        emu.HOLDER_PICK_INT: "долго не можем взять держатель",
        emu.LAMEL_PICK_INT: "долго не можем взять ламели",
        emu.LAMEL_TYPE_INT: "тип ламели на выдаче",
    }
    for index in emu.WELD_VALVE_INTS:
        names[index] = "клапан сварки"
    return names


def snapshot(plc, emu: ModuleType, runner: ScenarioRunner) -> dict:
    with plc.lock:
        frame = plc.build_status()
        state = {
            "mode": plc.mode,
            "modeName": emu.MODE_NAMES[plc.mode],
            "prepDone": plc.prep_done,
            "program": plc.program,
            "zone": plc.zone,
            "hmiFlags": list(plc.hmi_flags),
            "hmiLost": plc.hmi_lost,
            "hmiSeen": bool(plc.hmi_last_poll),
            "prepWaiting": plc.prep_waiting() if plc.mode == emu.MODE_RUN and not plc.prep_done else [],
            "inputs": {
                "mushroom": plc.mushroom,
                "doors": int(plc.doors_closed),
                "nolamel": int(plc.no_lamel),
                "noholders": int(plc.no_holders),
                "weldfail": int(plc.weld_timeout),
                "holderpick": int(plc.holder_pick),
                "lamelpick": int(plc.lamel_pick),
                "weld": int(plc.ready["weld"]),
                "vision1": int(plc.ready["vision1"]),
                "vision2": int(plc.ready["vision2"]),
                "holdersBig": int(plc.holders["big"]),
                "holdersSmall": int(plc.holders["small"]),
                "lameltype": plc.lamel_type,
            },
            "overrides": {str(k): v for k, v in plc.overrides.items()},
            "robotDo": list(plc.robot_do),
        }
    state["frame"] = [emu.get_i16(frame, i) for i in range(emu.STATUS_INT_COUNT)]
    state["log"] = list(emu.LOG_TAIL)[-60:]
    state["scenario"] = runner.name
    return state


def start(plc, emu: ModuleType, host: str, port: int) -> None:
    runner = ScenarioRunner(plc, emu)
    names = cell_names(emu)
    scenarios = [{"id": sid, "title": title} for sid, title, _ in SCENARIOS]

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args) -> None:
            pass

        def _send(self, code: int, body: bytes, ctype: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _json(self, data, code: int = 200) -> None:
            self._send(code, json.dumps(data, ensure_ascii=False).encode("utf-8"),
                       "application/json; charset=utf-8")

        def do_GET(self) -> None:
            if self.path == "/":
                self._send(200, PAGE.encode("utf-8"), "text/html; charset=utf-8")
            elif self.path == "/api/state":
                self._json(snapshot(plc, emu, runner))
            elif self.path == "/api/meta":
                self._json({"names": {str(k): v for k, v in names.items()}, "scenarios": scenarios})
            else:
                self._send(404, b"not found", "text/plain")

        def do_POST(self) -> None:
            length = int(self.headers.get("Content-Length") or 0)
            try:
                body = json.loads(self.rfile.read(length) or b"{}")
            except ValueError:
                self._json({"result": "плохой JSON"}, 400)
                return
            if self.path == "/api/action":
                action = str(body.get("action", "")).strip()
                with plc.lock:
                    result = plc.action(action)
                emu.log(f"веб: {action} → {result}")
                self._json({"result": result})
            elif self.path == "/api/scenario":
                self._json({"result": runner.run(str(body.get("id", "")))})
            elif self.path == "/api/scenario/cancel":
                runner.cancel()
                self._json({"result": "сценарий отменён"})
            else:
                self._json({"result": "нет такого адреса"}, 404)

    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()


PAGE = """<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><title>Эмулятор ПЛК</title>
<style>
body{font-family:system-ui,sans-serif;margin:16px;background:#f5f6f8;color:#1f2126}
h2{font-size:15px;margin:0 0 8px;color:#555}
.grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}
.card{background:#fff;border:1px solid #dbdee3;border-radius:8px;padding:12px}
.wide{grid-column:1/-1}
#mode{font-size:28px;font-weight:700}
.m0{color:#737880}.m1{color:#2e8c47}.m2{color:#b07a00}.m3{color:#4c8ce6}.m4{color:#c73333}.m5{color:#7a4cc7}
button{font:inherit;padding:6px 10px;margin:3px;border:1px solid #c5c9d0;border-radius:6px;background:#fff;cursor:pointer}
button:hover{background:#f0f2f5}
button.on{background:#e3f4e8;border-color:#59ad6b}
button.off{background:#fdecec;border-color:#d96b6b}
button.danger{background:#c73333;color:#fff;border-color:#c73333}
.row{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #eee;padding:2px 0}
#frame{display:grid;grid-template-columns:repeat(10,1fr);gap:4px}
.cell{border:1px solid #e1e3e8;border-radius:4px;padding:3px;font-size:12px;cursor:pointer;background:#fafbfc}
.cell b{font-size:15px}.cell .n{color:#888;font-size:10px;height:24px;overflow:hidden}
.cell.named{background:#fff;border-color:#c5c9d0}.cell.nz b{color:#2e6fd6}
.cell.ov{border:2px solid #e08a00;background:#fff5e0}
#log{font-family:monospace;font-size:12px;white-space:pre-wrap;max-height:260px;overflow:auto;background:#fafbfc;padding:6px}
input{font:inherit;padding:5px;width:60%}
.muted{color:#888;font-size:13px}
</style></head><body>
<div class="grid">
 <div class="card">
  <h2>Режим</h2><div id="mode">—</div><div id="info" class="muted"></div>
  <div style="margin-top:8px">
   <button class="danger" onclick="act('estop')">E-STOP</button>
   <button onclick="act('start')">СТАРТ (корпус)</button>
   <button onclick="act('stop')">СТОП (корпус)</button>
  </div>
 </div>
 <div class="card"><h2>Входы ПЛК</h2><div id="inputs"></div></div>
 <div class="card"><h2>Сценарии</h2><div id="scenarios"></div>
  <div class="muted" id="scen">—</div><button onclick="post('/api/scenario/cancel',{})">Отменить сценарий</button></div>
 <div class="card wide"><h2>Кадр ПЛК int16[0..79] — клик по ячейке: подменить значение (пусто — снять подмену)</h2>
  <div id="frame"></div>
  <div style="margin-top:6px"><button onclick="act('unset all')">Снять все подмены</button></div></div>
 <div class="card wide"><h2>Команда эмулятора</h2>
  <input id="cmd" placeholder="например: set 69 2, reboot estop, holders big 0" onkeydown="if(event.key==='Enter')sendCmd()">
  <button onclick="sendCmd()">Отправить</button> <span id="res" class="muted"></span>
  <h2 style="margin-top:10px">Лог эмулятора</h2><div id="log"></div></div>
</div>
<script>
let names={};
const INPUTS=[
 ['mushroom','Грибок отжат','mushroom'],['doors','Двери закрыты','doors'],
 ['weld','Клапаны сварки в 1','ready weld'],['vision1','Vision 1','ready vision1'],['vision2','Vision 2','ready vision2'],
 ['holdersBig','Большие держатели','holders big'],['holdersSmall','Малые держатели','holders small'],
 ['nolamel','Долго нет ламелей (авария)','nolamel',1],['noholders','Долго нет держателей (авария)','noholders',1],
 ['weldfail','Сварка долго не отрабатывает (авария)','weldfail',1],
 ['holderpick','Долго не можем взять держатель (авария)','holderpick',1],
 ['lamelpick','Долго не можем взять ламели (авария)','lamelpick',1]];
async function post(url,body){const r=await fetch(url,{method:'POST',body:JSON.stringify(body)});
 const j=await r.json();document.getElementById('res').textContent=j.result||'';refresh();}
function act(a){return post('/api/action',{action:a});}
function sendCmd(){const el=document.getElementById('cmd');if(el.value.trim())act(el.value.trim());}
function edit(i,cur){const v=prompt('int16['+i+'] '+(names[i]||'')+'\\nНовое значение (пусто — снять подмену):',cur);
 if(v===null)return; act(v.trim()===''?'unset '+i:'set '+i+' '+v.trim());}
function render(s){
 const m=document.getElementById('mode');m.textContent=s.mode+' · '+s.modeName+(s.mode===3?(s.prepDone?' · цикл':' · предподготовка'):'');m.className='m'+s.mode;
 const f=s.hmiFlags;document.getElementById('info').innerHTML=
  'программа: '+s.program+' · зона: '+s.zone+'<br>пульт: '+(!s.hmiSeen?'ещё не опрашивал':s.hmiLost?'<b style="color:#c73333">пропал</b>':'на связи')+
  ' · NATS '+f[0]+' CTRL '+f[1]+' Behaviour '+f[2]+' скрипт '+f[3]+
  (s.prepWaiting.length?'<br>Prep ждёт: '+s.prepWaiting.join(', '):'')+
  '<br>выходы от робота: '+s.robotDo.map((v,i)=>'K'+(i+1)+(v?'<b style="color:#2e8c47">='+v+'</b>':'=0')).join(' ');
 let h='';for(const [k,label,cmd,alarm] of INPUTS){const v=s.inputs[k];const bad=alarm?v:!v;
  h+='<div class="row"><span>'+label+'</span><button class="'+(bad?'off':'on')+'" onclick="act(\\''+cmd+' '+(v?0:1)+'\\')">'+(v?'да':'нет')+'</button></div>';}
 h+='<div class="row"><span>Тип ламели на выдаче</span><span>';
 for(const t of [0,1,2])h+='<button class="'+(s.inputs.lameltype===t?'on':'')+'" onclick="act(\\'lameltype '+t+'\\')">'+(t?'тип '+t:'пусто')+'</button>';
 document.getElementById('inputs').innerHTML=h+'</span></div>';
 let g='';s.frame.forEach((v,i)=>{const ov=(String(i) in s.overrides);
  g+='<div class="cell'+(names[i]?' named':'')+(v?' nz':'')+(ov?' ov':'')+'" title="'+(names[i]||'')+'" onclick="edit('+i+','+v+')">'+
   '<span class="muted">'+i+'</span> <b>'+v+'</b><div class="n">'+(names[i]||'')+'</div></div>';});
 document.getElementById('frame').innerHTML=g;
 document.getElementById('scen').textContent=s.scenario?'идёт: '+s.scenario:'сценарий не идёт';
 const lg=document.getElementById('log');lg.textContent=s.log.slice().reverse().join('\\n');
}
async function refresh(){try{render(await (await fetch('/api/state')).json());}catch(e){document.getElementById('mode').textContent='нет связи с эмулятором';}}
(async()=>{const meta=await (await fetch('/api/meta')).json();names=meta.names;
 document.getElementById('scenarios').innerHTML=meta.scenarios.map(x=>'<button onclick="post(\\'/api/scenario\\',{id:\\''+x.id+'\\'})">'+x.title+'</button>').join('');
 refresh();setInterval(refresh,700);})();
</script></body></html>
"""
