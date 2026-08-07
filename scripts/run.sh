#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/line-hmi-qt"
if [[ ! -x "$BIN" ]]; then
  cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build" -j"$(nproc)"
fi
exec "$BIN" "$@"
