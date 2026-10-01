#!/bin/bash
# Включает или выключает автовход на tty1, чтобы пульт стартовал без логина.
# Состояние читает админка: файл drop-in либо есть, либо нет.
#   hmi-autostart.sh on
#   hmi-autostart.sh off
set -euo pipefail

ACTION="${1:-}"
DROP_DIR=/etc/systemd/system/getty@tty1.service.d
CONF="$DROP_DIR/autologin.conf"

if [[ "$(id -u)" -ne 0 ]]; then
    echo "Нужны права root" >&2
    exit 1
fi

login_user() {
    if id scara >/dev/null 2>&1; then
        echo scara
        return
    fi
    if [[ -n "${SUDO_USER:-}" && "${SUDO_USER}" != root ]]; then
        echo "$SUDO_USER"
        return
    fi
    echo root
}

case "$ACTION" in
on)
    USER_NAME="$(login_user)"
    mkdir -p "$DROP_DIR"
    cat >"$CONF" <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin ${USER_NAME} --noclear %I \$TERM
EOF
    systemctl daemon-reload
    ;;
off)
    rm -f "$CONF"
    rmdir "$DROP_DIR" 2>/dev/null || true
    systemctl daemon-reload
    ;;
*)
    echo "Использование: hmi-autostart.sh on|off" >&2
    exit 1
    ;;
esac
