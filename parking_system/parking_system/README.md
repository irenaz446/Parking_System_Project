# Parking System

Embedded Linux parking management system.

| Part | Binary | Language | Description |
|------|--------|----------|-------------|
| 1 | `bin/server` | C++ | Async TCP server, fee calculation, shared memory |
| 2 | `bin/db` | C++ | SQLite persistence, shared memory poller |
| 2 | `bin/price_updater` | C | CLI tool — add / remove / list city prices |
| 3 | `bin/bbg_i2c` | C | BBG Process 2 — reads I2C from STM32, writes FIFO |
| 3 | `bin/bbg_tcp` | C | BBG Process 1 — reads FIFO, sends to TCP server |
| 3 | *(STM32 only)* | C | STM32 HAL GPS emulator |

## Build

```bash
sudo apt-get install build-essential libsqlite3-dev
make all
```

## Run

```bash
# Server + DB (on Linux host or BBG)
./scripts/start.sh

# BBG processes (on BeagleBone, after server is running)
./scripts/start_bbg.sh <SERVER_IP>

# Stop server + DB
./scripts/stop.sh
```

## Price management

```bash
./bin/price_updater list
./bin/price_updater add    TelAviv 0.55
./bin/price_updater remove Eilat
```

## Wire protocol

```
Client → Server:  S|<ID>|<LAT>,<LON>|<CITY>
Client → Server:  E|<ID>|<LAT>,<LON>|<CITY>
Server → Client:  OK:started
Server → Client:  OK:fee=5.00:elapsed=10.0min
Server → Client:  ERR:no_session
```

## Config files

| File | Component | Key settings |
|------|-----------|--------------|
| `config/server.cfg` | server | PORT, PRICES_FILE, LOG_FILE |
| `config/db.cfg` | db | DB_PATH, PRICES_FILE, POLL_INTERVAL |
| `config/prices.txt` | both | CITY,PRICE_PER_MINUTE |
