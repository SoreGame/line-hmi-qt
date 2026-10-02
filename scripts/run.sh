#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/line-hmi-qt"

# Не запускать GUI от root: sudo ломает доступ к X (:0 принадлежит scara).
if [[ "$(id -u)" -eq 0 ]]; then
  echo "Не запускайте run.sh через sudo." >&2
  echo "Сборка:  ./run.sh --build-only" >&2
  echo "Киоск:   sudo systemctl restart line-hmi.service" >&2
  exit 1
fi

# Киоск: если запускают по SSH без DISPLAY — экран :0.
export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-$HOME/.Xauthority}"

BUILD_ONLY=0
if [[ "${1:-}" == "--build-only" ]]; then
  BUILD_ONLY=1
  shift
fi

# Всегда прогоняем cmake --build: инкрементально и подхватывает правки исходников.
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build" -j"$(nproc)"

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  echo "Сборка готова: $BIN"
  exit 0
fi

# Киоск уже крутится — перезапускаем сервис, чтобы подхватил новый бинарник.
if systemctl is-active --quiet line-hmi.service 2>/dev/null; then
  echo "line-hmi.service активен — перезапуск…"
  sudo systemctl restart line-hmi.service
  systemctl is-active --quiet line-hmi.service
  echo "Готово."
  exit 0
fi

exec "$BIN" "$@"
