/**
 * @file server_main.cpp
 * @brief Entry point for the parking TCP server (Part 1).
 *
 * Responsibilities of main():
 *   1. Load config file.
 *   2. Initialise the logger.
 *   3. Attach shared memory.
 *   4. Initialise the inter-process rwlock (stored at a fixed offset
 *      inside shared memory so both server and DB use the same lock).
 *   5. Seed the pricing table from prices.txt.
 *   6. Install signal handlers (SIGINT / SIGTERM → graceful shutdown).
 *   7. Construct TcpServer and call run().
 *
 * Configuration keys (config/server.cfg)
 * ────────────────────────────────────────
 *   PORT          TCP listen port          (default 8080)
 *   PRICES_FILE   Initial prices path      (default config/prices.txt)
 *   LOG_FILE      Log path                 (default /tmp/parking_logs/server.log)
 */

#include <iostream>
#include <csignal>
#include <cstring>
#include <sys/stat.h>
#include <pthread.h>

#include "../common/common.h"
#include "../common/Logger.hpp"
#include "../common/Config.hpp"
#include "../common/SharedMemory.hpp"
#include "TcpServer.hpp"
#include "PriceTable.hpp"

/* ── Global so the signal handler can reach it ───────────────────────────── */
static TcpServer *g_server = nullptr;

static void sighandler(int)
{
    if (g_server) g_server->stop();
}

/* ── Inter-process rwlock stored in a static (non-shared) segment for now.
     Both server and DB are on the same host, so a process-shared mutex
     stored in shared memory would work too; for simplicity we use a
     PTHREAD_PROCESS_SHARED rwlock allocated alongside the SharedMemory
     object.  The lock is heap-allocated so its address is stable.        ── */
static pthread_rwlock_t g_shm_lock = PTHREAD_RWLOCK_INITIALIZER;

int main(int argc, char *argv[])
{
    const char *cfg_path = (argc > 1) ? argv[1] : "config/server.cfg";
    Config cfg(cfg_path);

    const std::string logFile    = cfg.get("LOG_FILE",    "/tmp/parking_logs/server.log");
    const std::string pricesFile = cfg.get("PRICES_FILE", DEFAULT_PRICES_FILE);
    const int         port       = cfg.getInt("PORT",     DEFAULT_PORT);

    mkdir(DEFAULT_LOG_DIR, 0755);
    Logger::init(logFile);
    Logger::info("=== Parking TCP Server (C++) starting ===");
    Logger::info("Port: " + std::to_string(port));
    Logger::info("Prices file: " + pricesFile);

    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    // Attach / create shared memory
    SharedMemory shm;
    Logger::info("Shared memory attached (id=" + std::to_string(shm.id()) + ")");

    // Seed prices into shared memory
    PriceTable pt(shm.data(), &g_shm_lock);
    int loaded = pt.loadFromFile(pricesFile);
    if (loaded < 0)
        Logger::warn("Could not open prices file: " + pricesFile);
    else
        Logger::info("Prices loaded: " + std::to_string(loaded) + " cities");

    // Build and run server
    try {
        TcpServer server(shm, &g_shm_lock, port);
        g_server = &server;
        server.run();
        g_server = nullptr;
    } catch (const std::exception &e) {
        Logger::error(std::string("Fatal: ") + e.what());
        Logger::close();
        return EXIT_FAILURE;
    }

    pthread_rwlock_destroy(&g_shm_lock);
    Logger::info("Server shut down cleanly");
    Logger::close();
    return EXIT_SUCCESS;
}
