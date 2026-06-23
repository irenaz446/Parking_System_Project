#!/usr/bin/env bash
# start.sh – Build (if needed) and launch the TCP server + database.
#
# Usage:  ./scripts/start.sh [--no-server] [--no-db]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/bin"
CFG="$ROOT/config"
LOG_DIR="/tmp/parking_logs"
PID_DIR="/tmp"

START_SERVER=true
START_DB=true
for arg in "$@"; do
    case "$arg" in
        --no-server) START_SERVER=false ;;
        --no-db)     START_DB=false ;;
    esac
done

mkdir -p "$LOG_DIR"

# Build if needed
if [[ ! -f "$BIN/server" || ! -f "$BIN/db" ]]; then
    echo "[*] Building..."
    make -C "$ROOT" all
fi

if $START_SERVER; then
    echo "[*] Starting TCP server..."
    "$BIN/server" "$CFG/server.cfg" &
    echo $! > "$PID_DIR/parking_server.pid"
    echo "[+] Server pid=$(cat $PID_DIR/parking_server.pid)"
fi

if $START_DB; then
    echo "[*] Starting database..."
    "$BIN/db" "$CFG/db.cfg" &
    echo $! > "$PID_DIR/parking_db.pid"
    echo "[+] DB pid=$(cat $PID_DIR/parking_db.pid)"
fi

echo
echo "Logs:  $LOG_DIR/"
echo "Stop:  ./scripts/stop.sh"
