/**
 * @file db_main.cpp
 * @brief Entry point for the parking database component (Part 2) — C++.
 *
 * Configuration keys (config/db.cfg):
 *   DB_PATH        SQLite file path         (default parking.db)
 *   PRICES_FILE    Prices text file         (default config/prices.txt)
 *   POLL_INTERVAL  Seconds between polls    (default 5)
 *   LOG_FILE       Log path                 (default /tmp/parking_logs/db.log)
 *   PID_FILE       PID file path            (default /tmp/parking_db.pid)
 */

#include <iostream>
#include <memory>       /* fix: was missing — needed for unique_ptr, make_unique */
#include <csignal>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>

#include "../common/common.h"
#include "../common/Logger.hpp"
#include "../common/Config.hpp"
#include "../common/SharedMemory.hpp"
#include "Database.hpp"
#include "PriceReloader.hpp"
#include "ShmPoller.hpp"

/* ── Signal flags ────────────────────────────────────────────────────────── */
static volatile sig_atomic_t g_running      = 1;
static volatile sig_atomic_t g_reloadPrices = 0;

static pthread_rwlock_t g_shm_lock = PTHREAD_RWLOCK_INITIALIZER;

static void sig_term(int) { g_running      = 0; }
static void sig_usr1(int) { g_reloadPrices = 1; }

/* ── PID file ────────────────────────────────────────────────────────────── */
static void writePidFile(const std::string &path)
{
    FILE *fp = fopen(path.c_str(), "w");
    if (!fp) { Logger::warn("Cannot write PID file: " + path); return; }
    fprintf(fp, "%d\n", static_cast<int>(getpid()));
    fclose(fp);
    Logger::info("PID file: " + path + " (pid=" + std::to_string(getpid()) + ")");
}

static void removePidFile(const std::string &path)
{
    remove(path.c_str());
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *cfgPath = (argc > 1) ? argv[1] : "config/db.cfg";
    Config cfg(cfgPath);

    const std::string logFile      = cfg.get("LOG_FILE",       "/tmp/parking_logs/db.log");
    const std::string dbPath       = cfg.get("DB_PATH",        DEFAULT_DB_PATH);
    const std::string pricesFile   = cfg.get("PRICES_FILE",    DEFAULT_PRICES_FILE);
    const std::string pidFile      = cfg.get("PID_FILE",       DEFAULT_PID_FILE);
    const int         pollInterval = cfg.getInt("POLL_INTERVAL", 5);

    mkdir(DEFAULT_LOG_DIR, 0755);
    Logger::init(logFile);
    Logger::info("=== Parking Database (C++) starting ===");
    Logger::info("DB path      : " + dbPath);
    Logger::info("Prices file  : " + pricesFile);
    Logger::info("Poll interval: " + std::to_string(pollInterval) + "s");

    signal(SIGINT,  sig_term);
    signal(SIGTERM, sig_term);
    signal(SIGUSR1, sig_usr1);

    writePidFile(pidFile);

    /* ── Open SQLite ──────────────────────────────────────────────────────── */
    std::unique_ptr<Database> db;
    try {
        db = std::make_unique<Database>(dbPath);
    } catch (const std::exception &e) {
        Logger::error(std::string("Database init failed: ") + e.what());
        Logger::close();
        removePidFile(pidFile);
        return EXIT_FAILURE;
    }

    /* ── Attach shared memory ─────────────────────────────────────────────── */
    std::unique_ptr<SharedMemory> shm;
    try {
        shm = std::make_unique<SharedMemory>();
    } catch (const std::exception &e) {
        Logger::error(std::string("Shared memory failed: ") + e.what());
        Logger::close();
        removePidFile(pidFile);
        return EXIT_FAILURE;
    }
    Logger::info("Shared memory attached (id=" + std::to_string(shm->id()) + ")");

    /* ── Build helpers ────────────────────────────────────────────────────── */
    PriceReloader reloader(*db, shm->data(), &g_shm_lock, pricesFile);
    ShmPoller     poller  (*db, shm->data(), &g_shm_lock);

    /* ── Initial price load ───────────────────────────────────────────────── */
    int loaded = reloader.reload();
    if (loaded < 0)
        Logger::warn("Prices file not found: " + pricesFile);
    else
        Logger::info("Initial prices loaded: " + std::to_string(loaded) + " entries");

    /* ── Main loop ────────────────────────────────────────────────────────── */
    Logger::info("Entering main loop (poll every " +
                 std::to_string(pollInterval) + "s)");

    while (g_running) {
        if (g_reloadPrices) {
            g_reloadPrices = 0;
            Logger::info("SIGUSR1 received — reloading prices");
            reloader.reload();
        }

        int persisted = poller.poll();
        if (persisted > 0)
            Logger::info("Persisted " + std::to_string(persisted) +
                         " new record(s) to DB");

        sleep(static_cast<unsigned>(pollInterval));
    }

    /* ── Cleanup ──────────────────────────────────────────────────────────── */
    pthread_rwlock_destroy(&g_shm_lock);
    removePidFile(pidFile);
    Logger::info("Database shut down cleanly");
    Logger::close();
    return EXIT_SUCCESS;
}
