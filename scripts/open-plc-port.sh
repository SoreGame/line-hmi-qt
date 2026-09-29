#!/usr/bin/env bash
# Открыть TCP 1502 для эмулятора/ПЛК (телефон в той же LAN).
set -euo pipefail
if command -v ufw >/dev/null 2>&1; then
    sudo ufw allow 1502/tcp comment 'line-hmi-qt PLC emulator'
    sudo ufw status | grep 1502 || true
else
    echo "ufw не найден — откройте порт 1502/tcp вручную в firewall."
fi
echo "Проверка с телефона (adb):"
echo "  timeout 3 nc 192.168.11.123 1502   # exit 0 = OK, 124 = всё ещё закрыт"
