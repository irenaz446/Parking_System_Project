# Parking System

## Overview

Three-component parking system: STM32 (I2C) → BeagleBone Green (PIPE/TCP) → PC (Server/DB)

```
STM32 (I2C Slave)  →  BBG (Process 2: I2C→PIPE)  →  PC (TCP Server)
                   →  BBG (Process 1: PIPE→TCP)  →  PC (SQLite DB)
```

---

## Quick Start

### PC Setup
```bash
sudo apt-get install build-essential libsqlite3-dev sqlite3
cd parking_system
make all

# Terminal 1
./bin/server config/server.cfg

# Terminal 2
./bin/db config/db.cfg
```

### BBG Setup
```bash
# Terminal 1 (start first)
sudo ./bbg_i2c

# Terminal 2
./bbg_tcp 192.168.10.1 8080
```

### STM32 Setup
1. Open STM32CubeIDE → Open `stm32/` folder
2. Build & Flash via Debug/Run
3. Connect I2C to BBG (with 4.7kΩ pull-ups on SCL/SDA)

---

## Network Configuration

**PC:**
```bash
sudo ip addr add 192.168.10.1/24 dev <INTERFACE>
sudo ip link set <INTERFACE> up
```

**BBG:**
```bash
sudo ip addr add 192.168.10.2/24 dev eth0
sudo ip link set eth0 up
```

---

## Database Queries

```bash
sqlite3 parking.db "SELECT customer_id, city, total_fee FROM customer_data;"
sqlite3 parking.db "SELECT city, COUNT(*) as sessions, SUM(total_fee) FROM customer_data GROUP BY city;"
```

---

## Wire Protocol

**Message format:** `TYPE|ID|LAT,LON|CITY\n`
- Start: `S|CAR-001|32.0853,34.7817|TelAviv`
- End: `E|CAR-001|32.0853,34.7817|TelAviv`

**Server replies:** `OK:started` or `OK:fee=X.XX:elapsed=Ymin`

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| BBG I2C fails | Start `bbg_i2c` first, check pull-ups |
| TCP connection fails | Check server running & IP address |
| No DB records | Start `./bin/db` |
| Missing sqlite3 | `sudo apt-get install libsqlite3-dev` |
```
