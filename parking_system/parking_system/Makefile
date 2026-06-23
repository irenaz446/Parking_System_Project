# ══════════════════════════════════════════════════════════════════════════
# Parking System – Root Makefile
#
# Targets:
#   all            Build all four binaries
#   server         Build the TCP server  (C++)
#   db             Build the DB + price_updater (C++ + C)
#   bbg            Build the two BBG processes (C)
#   clean          Remove bin/
#   install        Copy binaries to /usr/local/bin
# ══════════════════════════════════════════════════════════════════════════

CXX      := g++
CC       := gcc

CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -g -O2 -I./common \
            -Wno-stringop-truncation
CFLAGS   := -Wall -Wextra -Wpedantic -g -O2 -I./common

LDFLAGS  :=
LIBS     := -lpthread -lm
SQLIBS   := -lsqlite3

# Common C sources compiled into BBG and price_updater
COMMON_C := common/logger.c common/config.c

.PHONY: all server db bbg clean install

all: server db bbg

# ── TCP Server (C++) ───────────────────────────────────────────────────────
SERVER_SRC := server/server_main.cpp server/TcpServer.cpp
SERVER_HDR := server/TcpServer.hpp server/Session.hpp \
              server/SessionManager.hpp server/PriceTable.hpp \
              server/MessageParser.hpp \
              common/common.h common/Logger.hpp \
              common/Config.hpp common/SharedMemory.hpp

server: bin/server

bin/server: $(SERVER_SRC) $(SERVER_HDR) | bin
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS) $(LIBS)
	@echo "[OK] bin/server"

# ── Database (C++) + price_updater (C) ────────────────────────────────────
DB_SRC  := db/db_main.cpp db/Database.cpp
DB_HDR  := db/Database.hpp db/PriceReloader.hpp db/ShmPoller.hpp \
           common/common.h common/Logger.hpp \
           common/Config.hpp common/SharedMemory.hpp

db: bin/db bin/price_updater

bin/db: $(DB_SRC) $(DB_HDR) | bin
	$(CXX) $(CXXFLAGS) -o $@ $(DB_SRC) $(LDFLAGS) $(LIBS) $(SQLIBS)
	@echo "[OK] bin/db"

bin/price_updater: db/price_updater.c common/common.h | bin
	$(CC) $(CFLAGS) -o $@ db/price_updater.c $(LDFLAGS)
	@echo "[OK] bin/price_updater"

# ── BBG processes (C) ─────────────────────────────────────────────────────
bbg: bin/bbg_i2c bin/bbg_tcp

bin/bbg_i2c: bbg/bbg_i2c.c $(COMMON_C) common/common.h common/logger.h | bin
	$(CC) $(CFLAGS) -o $@ bbg/bbg_i2c.c $(COMMON_C) $(LDFLAGS) $(LIBS)
	@echo "[OK] bin/bbg_i2c"

bin/bbg_tcp: bbg/bbg_tcp.c $(COMMON_C) common/common.h common/logger.h | bin
	$(CC) $(CFLAGS) -o $@ bbg/bbg_tcp.c $(COMMON_C) $(LDFLAGS) $(LIBS)
	@echo "[OK] bin/bbg_tcp"

# ── Helpers ────────────────────────────────────────────────────────────────
bin:
	mkdir -p bin

clean:
	rm -rf bin
	@echo "[OK] cleaned"

install: all
	install -d /usr/local/bin
	install -m 755 bin/server        /usr/local/bin/parking_server
	install -m 755 bin/db            /usr/local/bin/parking_db
	install -m 755 bin/price_updater /usr/local/bin/parking_prices
	install -m 755 bin/bbg_i2c       /usr/local/bin/parking_bbg_i2c
	install -m 755 bin/bbg_tcp       /usr/local/bin/parking_bbg_tcp
	@echo "[OK] installed"
