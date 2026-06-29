/**
 * @file server_main.cpp
 * @brief Entry point for the parking TCP server (Part 1).
 *
 * Configuration keys (config/server.cfg):
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

static TcpServer *g_server = nullptr;

static void sighandler(int)
{
    if (g_server) g_server->stop();
}

static pthread_rwlock_t g_shm_lock = PTHREAD_RWLOCK_INITIALIZER;

/* Helper: print to BOTH terminal and log file */
static void printAndLog(const std::string &msg)
{
    std::cout << msg << std::endl;
    Logger::info(msg);
}

int main(int argc, char *argv[])
{
    const char *cfg_path = (argc > 1) ? argv[1] : "config/server.cfg";
    Config cfg(cfg_path);

    const std::string logFile    = cfg.get("LOG_FILE",    "/tmp/parking_logs/server.log");
    const std::string pricesFile = cfg.get("PRICES_FILE", DEFAULT_PRICES_FILE);
    const int         port       = cfg.getInt("PORT",     DEFAULT_PORT);

    /* Create log directory and init logger */
    mkdir(DEFAULT_LOG_DIR, 0755);
    Logger::init(logFile);

    /* Print to terminal AND log file */
    printAndLog("=== Parking TCP Server (C++) starting ===");
    printAndLog("Port        : " + std::to_string(port));
    printAndLog("Prices file : " + pricesFile);
    printAndLog("Log file    : " + logFile);

    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    /* Attach shared memory */
    SharedMemory shm;
    printAndLog("Shared memory attached (id=" + std::to_string(shm.id()) + ")");

    /* Load prices into shared memory */
    PriceTable pt(shm.data(), &g_shm_lock);
    int loaded = pt.loadFromFile(pricesFile);
    if (loaded < 0)
        printAndLog("WARNING: Could not open prices file: " + pricesFile);
    else
        printAndLog("Prices loaded: " + std::to_string(loaded) + " cities");

    /* Build and run server */
    try {
        TcpServer server(shm, &g_shm_lock, port);
        g_server = &server;
        printAndLog("Listening on port " + std::to_string(port));
        printAndLog("Waiting for BBG connections...");
        server.run();
        g_server = nullptr;
    } catch (const std::exception &e) {
        std::cerr << "Fatal: " << e.what() << std::endl;
        Logger::error(std::string("Fatal: ") + e.what());
        Logger::close();
        return EXIT_FAILURE;
    }

    pthread_rwlock_destroy(&g_shm_lock);
    printAndLog("Server shut down cleanly");
    Logger::close();
    return EXIT_SUCCESS;
}
