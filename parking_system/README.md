# Parking System — Complete Project Guide

## Project Overview

A real-time parking management system spanning three hardware components:

```
STM32 (I2C Slave)          BeagleBone Green (Linux)         PC (Linux)
──────────────────         ────────────────────────         ──────────
Simulates GPS data    I2C  Process 2: bbg_i2c          TCP  bin/server
4 vehicles cycling ──────► reads frames, writes pipe ──────► calculates fees
                           Process 1: bbg_tcp               bin/db
                           reads pipe, sends TCP             saves to SQLite
```

---

## Project Directory Structure

```
parking_system/
├── common/                  # Shared C/C++ headers and sources
│   ├── common.h             # Shared structs, constants (C and C++)
│   ├── Logger.hpp           # C++ thread-safe logger
│   ├── Config.hpp           # C++ config file parser
│   ├── SharedMemory.hpp     # C++ RAII shared memory wrapper
│   ├── logger.h / logger.c  # C logger (for BBG processes)
│   └── config.h / config.c  # C config parser (for BBG processes)
├── server/                  # TCP server (C++)
│   ├── server_main.cpp      # Entry point
│   ├── TcpServer.hpp/.cpp   # poll()-based async server
│   ├── Session.hpp          # Active parking session
│   ├── SessionManager.hpp   # Thread-safe session store
│   ├── PriceTable.hpp       # City price lookup from shared memory
│   └── MessageParser.hpp    # Wire message parser
├── db/                      # Database component (C++ + C)
│   ├── db_main.cpp          # Entry point
│   ├── Database.hpp/.cpp    # SQLite wrapper
│   ├── PriceReloader.hpp    # Reloads prices on SIGUSR1
│   ├── ShmPoller.hpp        # Polls shared memory for new records
│   └── price_updater.c      # CLI tool to update prices (C)
├── bbg/                     # BeagleBone Green processes (C)
│   ├── bbg_i2c.c            # Process 2: I2C master → named PIPE
│   └── bbg_tcp.c            # Process 1: named PIPE → TCP server
├── stm32/                   # STM32CubeIDE project (see note below)
│   └── [CubeIDE project]    # Compiled separately — NOT by Makefile
├── config/
│   ├── server.cfg           # Server configuration
│   ├── db.cfg               # Database configuration
│   └── prices.txt           # City pricing table
├── scripts/
│   ├── start.sh             # Start server + DB on PC
│   ├── stop.sh              # Stop server + DB
│   └── start_bbg.sh         # Start both BBG processes
├── Makefile                 # Builds server, db, bbg, price_updater
└── README.md                # This file
```

---

## Part 1 — STM32 Setup (STM32CubeIDE)

### About the stm32/ folder

The `stm32/` folder contains your **STM32CubeIDE project**.
It is stored here for organisation only.
The Makefile does **not** touch it — STM32 code is compiled exclusively
by STM32CubeIDE using the ARM cross-compiler it bundles.

### CubeMX peripheral configuration

Open the `.ioc` file in STM32CubeIDE and verify:

| Peripheral | Setting | Value |
|-----------|---------|-------|
| I2C2 | Mode | I2C (slave) |
| I2C2 | Own Address 1 | 16 (= 0x08 << 1) |
| I2C2 | Timing | 0x00303D5B |
| TIM2 | Prescaler | 15999 |
| TIM2 | Counter Period | 999 |
| TIM2 | Global interrupt | Enabled |
| USART3 | Mode | Asynchronous |
| USART3 | Baud rate | 115200 |

Timer gives exactly 1-second interrupts:
`16 MHz / (15999+1) / (999+1) = 1 Hz`

### Wiring — STM32 to BBG

```
STM32 NUCLEO              BeagleBone Green
─────────────             ────────────────
PB10 (SCL) ─────────────► P9_19 (SCL, I2C2)
PB11 (SDA) ─────────────► P9_20 (SDA, I2C2)
GND        ─────────────► P9_1  (GND)

Pull-up resistors (mandatory!):
  4.7kΩ from SCL to 3.3V
  4.7kΩ from SDA to 3.3V
```

### Build and flash

1. Open STM32CubeIDE
2. File → Open Projects from File System → select `stm32/` folder
3. Copy `main.c` (from `stm32/` project) into `Core/Src/main.c`
4. Copy `io_tools.c` into `Core/Src/io_tools.c`
5. Build: Project → Build All (Ctrl+B)
6. Flash: Run → Debug (or Run → Run)

### UART debug output

Connect the Nucleo USB cable to your PC.
Open a serial monitor at **115200 baud**:

```bash
# Linux
screen /dev/ttyACM0 115200
# or
minicom -b 115200 -D /dev/ttyACM0
```

Expected output after flash:
```
=== STM32 I2C Slave started ===
Slave addr   : 0x08
Frame size   : 48 bytes
Num vehicles : 4
Waiting for BBG master...
```

---

## Part 2 — PC Setup (Linux)

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install build-essential libsqlite3-dev sqlite3
```

### Build

```bash
cd parking_system
make all
```

Expected output:
```
[OK] bin/server
[OK] bin/db
[OK] bin/price_updater
[OK] bin/bbg_i2c
[OK] bin/bbg_tcp
```

Individual targets:
```bash
make server        # build only the TCP server
make db            # build only db + price_updater
make bbg           # build only BBG binaries
make clean         # remove bin/ directory
```

### Network setup — direct Ethernet cable (recommended)

Connect a single Ethernet cable between PC and BBG.

**On PC:**
```bash
# Replace enp3s0 with your actual Ethernet interface (check with: ip link show)
sudo ip addr add 192.168.10.1/24 dev enp3s0
sudo ip link set enp3s0 up
```

**On BBG:**
```bash
sudo ip addr add 192.168.10.2/24 dev eth0
sudo ip link set eth0 up
```

Test:
```bash
ping 192.168.10.2   # from PC to BBG — should work
ping 192.168.10.1   # from BBG to PC — should work
```

### Configuration files

**`config/server.cfg`**
```
PORT          = 8080
PRICES_FILE   = config/prices.txt
LOG_FILE      = /tmp/parking_logs/server.log
```

**`config/db.cfg`**
```
DB_PATH        = parking.db
PRICES_FILE    = config/prices.txt
POLL_INTERVAL  = 5
LOG_FILE       = /tmp/parking_logs/db.log
PID_FILE       = /tmp/parking_db.pid
```

**`config/prices.txt`**
```
# Format: CITY_NAME,PRICE_PER_MINUTE
TelAviv,0.50
Jerusalem,0.35
Haifa,0.30
BeerSheva,0.20
Eilat,0.40
Netanya,0.25
```

### Run the server and database

Open two terminals on the PC:

**Terminal 1 — TCP server:**
```bash
cd parking_system
./bin/server config/server.cfg
```

Expected:
```
=== Parking TCP Server (C++) starting ===
Port        : 8080
Prices file : config/prices.txt
Prices loaded: 6 cities
Listening on port 8080
Waiting for BBG connections...
```

**Terminal 2 — database:**
```bash
cd parking_system
./bin/db config/db.cfg
```

Expected:
```
=== Parking Database (C++) starting ===
DB path      : parking.db
Prices loaded: 6 entries
Waiting for parking sessions...
```

### Monitor activity (live logs)

```bash
tail -f /tmp/parking_logs/server.log /tmp/parking_logs/db.log
```

---

## Part 3 — BBG Setup

### Copy binaries to BBG

```bash
# Run on PC — copy BBG binaries
scp parking_system/bin/bbg_i2c  debian@192.168.10.2:~
scp parking_system/bin/bbg_tcp  debian@192.168.10.2:~
```

Or compile directly on the BBG:

```bash
# Run on BBG
gcc bbg_i2c.c -o bbg_i2c
gcc bbg_tcp.c -o bbg_tcp
```

### Run BBG processes

The two processes communicate via a named PIPE (`/tmp/parking.pipe`).
**Process 2 must start first** because it creates the PIPE.

**BBG Terminal 1 — Process 2 (I2C reader):**
```bash
sudo ./bbg_i2c
```

Expected:
```
[I2C] Opened /dev/i2c-2, slave=0x08
[PIPE] FIFO created: /tmp/parking.pipe
[PIPE] Waiting for Process 1 (bbg_tcp) to connect...
```

**BBG Terminal 2 — Process 1 (TCP client):**
```bash
./bbg_tcp 192.168.10.1 8080
```

Process 2 immediately unblocks and both start working:
```
[PIPE] Process 1 connected — pipe ready
=== BBG Process 2: I2C→PIPE started ===

[I2C] type=S id=CAR-001 lat=32.0853 lon=34.7817 city=TelAviv
[PIPE→] S|CAR-001|32.0853,34.7817|TelAviv
```

```
[PIPE] Opened /tmp/parking.pipe for reading
=== BBG Process 1: PIPE→TCP started ===
[TCP] Connected to server 192.168.10.1:8080
[←PIPE] S|CAR-001|32.0853,34.7817|TelAviv
[TCP] Server reply: OK:started
...
[←PIPE] E|CAR-001|32.0853,34.7817|TelAviv
[TCP] Server reply: OK:fee=0.08:elapsed=10.0min
```

---

## Viewing the Database

```bash
# All parking sessions
sqlite3 parking.db "SELECT customer_id, city, start_time, end_time, total_fee
                    FROM customer_data;"

# With column headers and alignment
sqlite3 -column -header parking.db \
  "SELECT customer_id, city, total_fee FROM customer_data;"

# Count sessions per city
sqlite3 parking.db \
  "SELECT city, COUNT(*) as sessions, SUM(total_fee) as revenue
   FROM customer_data GROUP BY city;"

# Current prices
sqlite3 parking.db "SELECT city, price_per_min FROM prices;"
```

---

## Price Management (CLI)

```bash
# List current prices
./bin/price_updater list

# Update a price (DB and server reload immediately)
./bin/price_updater add TelAviv 1.00

# Remove a city
./bin/price_updater remove Eilat

# Add a new city
./bin/price_updater add Netanya 0.30
```

When you run `add` or `remove`, the tool:
1. Updates `config/prices.txt`
2. Sends `SIGUSR1` to the DB process
3. DB reloads prices into shared memory
4. Server immediately uses the new prices

---

## Full Startup Sequence

```
1. Flash STM32 via STM32CubeIDE
2. Power on STM32 board (USB cable)
3. Connect STM32 ↔ BBG via I2C + pull-up resistors
4. Connect BBG ↔ PC via Ethernet cable
5. On PC:    ./bin/server config/server.cfg     (Terminal 1)
6. On PC:    ./bin/db config/db.cfg             (Terminal 2)
7. On BBG:   sudo ./bbg_i2c                     (Terminal 3)
8. On BBG:   ./bbg_tcp 192.168.10.1 8080        (Terminal 4)
9. Watch:    tail -f /tmp/parking_logs/server.log
```

---

## Wire Protocol Reference

Messages sent from BBG to TCP server (newline-terminated):

| Direction | Format | Example |
|-----------|--------|---------|
| Start parking | `S\|ID\|LAT,LON\|CITY` | `S\|CAR-001\|32.0853,34.7817\|TelAviv` |
| End parking | `E\|ID\|LAT,LON\|CITY` | `E\|CAR-001\|32.0853,34.7817\|TelAviv` |

Server replies:

| Event | Reply |
|-------|-------|
| Start accepted | `OK:started` |
| End + fee | `OK:fee=0.08:elapsed=10.0min` |
| Unknown car | `ERR:no_session` |
| Bad message | `ERR:bad_msg` |

---

## I2C Frame Format (STM32 → BBG)

```
Byte  0      : type — 'S' (start) or 'E' (end)
Bytes 1-15   : customer_id — "CAR-001\0"
Bytes 16-23  : latitude    — "32.0853\0"
Bytes 24-31  : longitude   — "34.7817\0"
Bytes 32-47  : city        — "TelAviv\0"
Total        : 48 bytes (packed, no padding)
```

---

## Troubleshooting

| Problem | Likely cause | Fix |
|---------|-------------|-----|
| STM32 prints `[ERR] I2C failed` | BBG not running or wrong wiring | Start bbg_i2c first, check pull-ups |
| BBG prints `[ERR] I2C read` | STM32 not powered or not flashed | Power/reflash STM32 |
| BBG prints `[ERR] connect failed` | Server not running or wrong IP | Start server, check IP address |
| No records in SQLite | DB not running | Start bin/db |
| `make db` errors | Missing libsqlite3 | `sudo apt-get install libsqlite3-dev` |
| Pipe blocks forever | Process 2 waiting for Process 1 | Start bbg_tcp in second terminal |
