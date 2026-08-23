#!/usr/bin/env bash
# Flash every ESP32-P4 that appears on USB TO UART (/dev/ttyACM*).
# Unplug after SUCCESS, plug the next tape. Ctrl-C to stop.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BIN="${BIN:-$ROOT/.pio/build/mm1_p4/firmware.bin}"
ENV="${ENV:-mm1_p4}"

if [[ ! -f "$BIN" ]]; then
  echo "Building $ENV…"
  VERSION="$(git describe --tags --always --dirty)" pio run -e "$ENV"
fi

echo "Watching USB for P4 (QinHeng / ttyACM). Unplug each board after SUCCESS."
seen=""
flashed=0
while true; do
  ports="$(ls /dev/ttyACM* 2>/dev/null || true)"
  if [[ -z "$ports" ]]; then
    seen=""
    sleep 1
    continue
  fi
  for port in $ports; do
    case " $seen " in
      *" $port "*) continue ;;
    esac
    echo
    echo "=== $port  #$((flashed + 1)) ==="
    if VERSION="$(git describe --tags --always --dirty)" pio run -e "$ENV" -t upload --upload-port "$port"; then
      flashed=$((flashed + 1))
      seen="$seen $port"
      echo "SUCCESS #$flashed on $port — unplug and connect the next tape."
    else
      echo "FAIL on $port — check the cable (USB TO UART, not OTG) and retry."
      sleep 2
    fi
  done
  sleep 1
done
