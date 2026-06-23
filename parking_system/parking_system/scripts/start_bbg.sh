#!/usr/bin/env bash
# start_bbg.sh – Launch both BBG processes on the BeagleBone Green.
#
# Usage:  ./scripts/start_bbg.sh <SERVER_IP> [SERVER_PORT]

set -euo pipefail

SERVER_IP="${1:?Usage: $0 <SERVER_IP> [PORT]}"
SERVER_PORT="${2:-8080}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/bin"
PIPE="/tmp/parking.pipe"
LOG_DIR="/tmp/parking_logs"

mkdir -p "$LOG_DIR"

echo "[*] Starting I2C receiver (Process 2)..."
"$BIN/bbg_i2c" /dev/i2c-1 0x08 "$PIPE" "$LOG_DIR/bbg_i2c.log" 0 &
echo "[+] bbg_i2c pid=$!"

sleep 1   # give Process 2 time to create the FIFO

echo "[*] Starting TCP client (Process 1)..."
"$BIN/bbg_tcp" "$SERVER_IP" "$SERVER_PORT" "$PIPE" "$LOG_DIR/bbg_tcp.log" &
echo "[+] bbg_tcp pid=$!"

echo
echo "BBG running. Press Ctrl-C to stop."
wait
