/**
 * @file PriceReloader.hpp
 * @brief Reloads the prices file into the SQLite DB and shared memory.
 *
 * Called by the DB main loop whenever the g_reloadPrices flag is set
 * (which happens inside the SIGUSR1 handler).
 *
 * Prices file format
 * ───────────────────
 *   CITY_NAME,PRICE_PER_MINUTE   → upsert
 *   -CITY_NAME                   → delete
 *   # comment                    → ignored
 *
 * The reload wraps all DB writes in a single transaction, then rebuilds
 * the shared memory prices array under a write-lock so the server
 * immediately picks up the new rates.
 */

#pragma once

#include <string>
#include <fstream>
#include <pthread.h>
#include "Database.hpp"
#include "../common/common.h"
#include "../common/Logger.hpp"

class PriceReloader {
public:
    PriceReloader(Database        &db,
                  shared_data_t   *shm,
                  pthread_rwlock_t *rwlock,
                  const std::string &pricesFile)
        : db_(db)
        , shm_(shm)
        , rwlock_(rwlock)
        , pricesFile_(pricesFile)
    {}

    /**
     * @brief Read the prices file and apply changes to DB + shared memory.
     * @return Number of entries processed, or -1 on file error.
     */
    int reload()
    {
        std::ifstream f(pricesFile_);
        if (!f.is_open()) {
            Logger::warn("PriceReloader: cannot open " + pricesFile_);
            return -1;
        }

        Logger::info("Reloading prices from " + pricesFile_);

        // Collect all changes first, then apply atomically
        struct Change {
            bool        remove;
            std::string city;
            double      price = 0.0;
        };
        std::vector<Change> changes;

        std::string line;
        while (std::getline(f, line)) {
            // Strip CR
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;

            if (line[0] == '-') {
                changes.push_back({true, line.substr(1), 0.0});
                continue;
            }

            auto comma = line.find(',');
            if (comma == std::string::npos) continue;

            Change c;
            c.remove = false;
            c.city   = line.substr(0, comma);
            try { c.price = std::stod(line.substr(comma + 1)); }
            catch (...) { continue; }
            changes.push_back(c);
        }

        if (changes.empty()) {
            Logger::info("PriceReloader: no entries found");
            return 0;
        }

        // Apply to DB in one transaction
        db_.transaction([&] {
            for (auto &c : changes) {
                if (c.remove) db_.deletePrice(c.city);
                else          db_.upsertPrice(c.city, c.price);
            }
        });

        // Rebuild shared memory prices array under write-lock
        pthread_rwlock_wrlock(rwlock_);
        shm_->price_count = 0;
        for (auto &c : changes) {
            if (c.remove) continue;
            if (shm_->price_count >= SHM_MAX_CITIES) break;
            auto &cp = shm_->prices[shm_->price_count++];
            strncpy(cp.city, c.city.c_str(), CITY_NAME_LEN - 1);
            cp.city[CITY_NAME_LEN - 1] = '\0';
            cp.price_per_minute = c.price;
            cp.active = 1;
        }
        pthread_rwlock_unlock(rwlock_);

        Logger::info("PriceReloader: applied " + std::to_string(changes.size())
                     + " changes, SHM has "
                     + std::to_string(shm_->price_count) + " active cities");

        return static_cast<int>(changes.size());
    }

private:
    Database         &db_;
    shared_data_t    *shm_;
    pthread_rwlock_t *rwlock_;
    std::string       pricesFile_;
};
