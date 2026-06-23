#!/usr/bin/env bash
# stop.sh – Gracefully stop server and DB.

stop_proc() {
    local name="$1" pidfile="$2"
    if [[ -f "$pidfile" ]]; then
        local pid; pid=$(cat "$pidfile")
        if kill -0 "$pid" 2>/dev/null; then
            echo "[*] Stopping $name (pid=$pid)..."
            kill -TERM "$pid"
            sleep 1
            kill -0 "$pid" 2>/dev/null && kill -KILL "$pid"
            echo "[+] $name stopped"
        else
            echo "[!] $name not running (stale PID)"
        fi
        rm -f "$pidfile"
    else
        echo "[!] No PID file for $name"
    fi
}

stop_proc "TCP Server" /tmp/parking_server.pid
stop_proc "Database"   /tmp/parking_db.pid
echo "Done."
