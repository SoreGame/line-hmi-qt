#!/usr/bin/env python3
"""Дописывает все тексты пульта в раздел "texts" config.json.

.cpp: каждый строковый литерал (вместе с соседними, которые C++ склеивает),
в котором есть кириллица. .ui: свойства text/title/windowTitle/placeholderText/
toolTip без notr="true".

Отредактированные значения не трогает. Тексты, которых больше нет в программе,
удаляет, только если их не правили.

Запуск: update_texts_config.py CONFIG.json SRC...
"""

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

CYRILLIC = re.compile(r"[А-Яа-яЁё]")
LETTER = re.compile(r"[A-Za-zА-Яа-яЁё]")
ESCAPE = re.compile(r"\\(u[0-9a-fA-F]{4}|x[0-9a-fA-F]{2}|.)")
SIMPLE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", '"': '"', "'": "'", "\\": "\\", "?": "?"}
EXCLUDED = {"Texts.cpp"}
UI_PROPERTIES = {"text", "title", "windowTitle", "placeholderText", "toolTip", "statusTip"}

COMMENT = (
    "Тексты пульта. Ключ — исходный текст из программы, его не менять; значение — что показывать. "
    "%1, %2… — подстановки, их не удалять. Одинаковый текст в разных разделах один и тот же. "
    "Значение, равное ключу, или пустое — текст из программы. "
    "Правка здесь попадает на пульт после сборки и перезапуска. "
    "На отдельном пульте можно переопределить тексты в ~/.config/SCARA/line-hmi-qt/config.json "
    "тем же разделом texts. Новые тексты сборка дописывает сама."
)

GROUPS = {
    "MainWindow.ui": "MainWindow.ui — надписи главного экрана",
    "MainWindow.cpp": "MainWindow — главный экран и журнал",
    "PrepOverlay.cpp": "PrepOverlay — окно предподготовки",
    "RecoveryOverlay.cpp": "RecoveryOverlay — окно аварии",
    "ServicePanel.cpp": "ServicePanel — сервисная панель",
    "AdminPanel.cpp": "AdminPanel — админка",
    "NatsClient.cpp": "NatsClient — ошибки NATS",
    "PlcClient.cpp": "PlcClient — ошибки ПЛК",
    "ArduinoLink.cpp": "ArduinoLink — тензодатчик",
    "ConnectionSettings.cpp": "ConnectionSettings — настройки",
}


def cpp_literal_groups(source: str):
    """Последовательности соседних строковых литералов: [[raw1, raw2, ...]]."""
    groups = []
    current = []
    i = 0
    n = len(source)
    while i < n:
        c = source[i]
        if source.startswith("//", i):
            end = source.find("\n", i)
            i = n if end < 0 else end + 1
            continue
        if source.startswith("/*", i):
            end = source.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        if c.isspace():
            i += 1
            continue
        if c == "'":
            j = i + 1
            while j < n and source[j] != "'":
                j += 2 if source[j] == "\\" else 1
            i = j + 1
            if current:
                groups.append(current)
                current = []
            continue
        if c == "R" and source.startswith('R"', i) and (i == 0 or not (source[i - 1].isalnum() or source[i - 1] == "_")):
            open_paren = source.find("(", i + 2)
            delim = source[i + 2:open_paren]
            end = source.find(")" + delim + '"', open_paren)
            i = n if end < 0 else end + len(delim) + 2
            if current:
                groups.append(current)
                current = []
            continue
        if c == '"':
            j = i + 1
            while j < n and source[j] != '"':
                j += 2 if source[j] == "\\" else 1
            current.append(source[i + 1:j])
            i = j + 1
            continue
        if current:
            groups.append(current)
            current = []
        i += 1
    if current:
        groups.append(current)
    return groups


def unescape_c(body: str) -> str:
    def repl(m):
        esc = m.group(1)
        if esc[0] in "ux" and len(esc) > 1:
            return chr(int(esc[1:], 16))
        return SIMPLE_ESCAPES.get(esc, esc)

    return ESCAPE.sub(repl, body)


def collect_cpp(path: Path):
    for pieces in cpp_literal_groups(path.read_text(encoding="utf-8")):
        text = unescape_c("".join(pieces))
        if CYRILLIC.search(text):
            yield text


def collect_ui(path: Path):
    root = ET.parse(path).getroot()
    for node in root.iter():
        if node.tag not in ("property", "attribute") or node.get("name") not in UI_PROPERTIES:
            continue
        string = node.find("string")
        if string is None or string.get("notr") == "true" or not LETTER.search(string.text or ""):
            continue
        yield string.text


def catalog(sources):
    order = {name: i for i, name in enumerate(GROUPS)}
    files = sorted((Path(p) for p in sources), key=lambda p: (order.get(p.name, len(order)), p.name))
    result = {}
    for src in files:
        if src.name in EXCLUDED:
            continue
        group = result.setdefault(GROUPS.get(src.name, src.name), [])
        collect = collect_ui if src.suffix == ".ui" else collect_cpp
        for text in collect(src):
            if text not in group:
                group.append(text)
    return result


def merge(old_texts: dict, wanted: dict) -> dict:
    merged = {}
    for group, texts in wanted.items():
        if not texts:
            continue
        old = old_texts.get(group, {})
        merged[group] = {text: old.get(text, text) for text in texts}
    for group, old in old_texts.items():
        for text, value in old.items():
            if isinstance(value, str) and value and value != text and text not in merged.get(group, {}):
                merged.setdefault(group, {})[text] = value
    return merged


def main():
    config_path = Path(sys.argv[1])
    original = config_path.read_text(encoding="utf-8")
    config = json.loads(original)

    texts = merge(config.get("texts", {}), catalog(sys.argv[2:]))
    config.pop("_texts_comment", None)
    config.pop("texts", None)
    config["_texts_comment"] = COMMENT
    config["texts"] = texts

    updated = json.dumps(config, ensure_ascii=False, indent=4) + "\n"
    if updated != original:
        config_path.write_text(updated, encoding="utf-8")


if __name__ == "__main__":
    main()
