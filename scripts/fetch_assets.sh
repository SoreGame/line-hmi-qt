#!/usr/bin/env bash
set -euo pipefail
OUT="/home/egor/repos/robot/line-hmi-qt/assets"
# args: name url
name="$1"
url="$2"
curl -fsSL -o "$OUT/${name}.png" "$url"
file "$OUT/${name}.png"
