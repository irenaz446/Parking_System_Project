/**
 * @file PriceTable.hpp
 * @brief Thread-safe city price lookup backed by shared memory.
 *
 * The server only reads prices; the DB process writes them.
 * A pthread_rwlock_t (stored in a shared location passed in from main)
 * protects concurrent access across both processes.
 *
 * Additionally, an in-process std::shared_mutex guards the in-memory
 * seed path so multiple server threads can call lookup() concurrently.
 */

#pragma once

#include <string>
#include <shared_mutex>
#include <pthread.h>
#include "../common/common.h"

class PriceTable {
public:
    /**
     * @brief Construct with a pointer to the prices array in shared memory
     *        and its associated inter-process rwlock.
     */
    PriceTable(shared_data_t *shm, pthread_rwlock_t *rwlock)
        : shm_(shm), rwlock_(rwlock)
    {}

    /**
     * @brief Look up the per-minute price for a city.
     * @return Price >= 0.0, or -1.0 if the city is unknown.
     */
    double lookup(const std::string &city) const
    {
        pthread_rwlock_rdlock(rwlock_);
        double price = -1.0;
        for (int i = 0; i < shm_->price_count; ++i) {
            const auto &cp = shm_->prices[i];
            if (cp.active && city == cp.city) {
                price = cp.price_per_minute;
                break;
            }
        }
        pthread_rwlock_unlock(rwlock_);
        return price;
    }

    /**
     * @brief Load initial prices from a text file into shared memory.
     *
     * File format (one entry per line):  CITY_NAME,PRICE_PER_MINUTE
     * Lines starting with '#' are comments.
     *
     * @param path  Path to the prices file.
     * @return Number of cities loaded, or -1 on file-open failure.
     */
    int loadFromFile(const std::string &path)
    {
        FILE *fp = fopen(path.c_str(), "r");
        if (!fp) return -1;

        pthread_rwlock_wrlock(rwlock_);
        shm_->price_count = 0;

        char line[256];
        while (fgets(line, sizeof(line), fp) &&
               shm_->price_count < SHM_MAX_CITIES)
        {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '#' || line[0] == '\0') continue;

            char *comma = strchr(line, ',');
            if (!comma) continue;
            *comma = '\0';

            auto &cp = shm_->prices[shm_->price_count];
            memset(cp.city, 0, CITY_NAME_LEN);
            strncpy(cp.city, line, CITY_NAME_LEN - 1);
            cp.price_per_minute = atof(comma + 1);
            cp.active = 1;
            ++shm_->price_count;
        }

        int loaded = shm_->price_count;
        pthread_rwlock_unlock(rwlock_);
        fclose(fp);
        return loaded;
    }

private:
    shared_data_t   *shm_;
    pthread_rwlock_t *rwlock_;
};
