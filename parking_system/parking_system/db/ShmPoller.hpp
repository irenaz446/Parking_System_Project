/**
 * @file ShmPoller.hpp
 * @brief Polls the shared memory ring-buffer for new completed records.
 *
 * The server increments shm->record_count each time it stores a session.
 * ShmPoller keeps a local watermark (lastSeen_) and reads every new slot,
 * persisting each completed record to the database.
 *
 * poll() is called from the DB main loop on a fixed interval.
 */

#pragma once

#include <pthread.h>
#include "Database.hpp"
#include "../common/common.h"
#include "../common/Logger.hpp"

class ShmPoller {
public:
    ShmPoller(Database        &db,
              shared_data_t   *shm,
              pthread_rwlock_t *rwlock)
        : db_(db), shm_(shm), rwlock_(rwlock)
    {}

    /**
     * @brief Check for and persist any new completed records.
     * @return Number of records newly persisted.
     */
    int poll()
    {
        pthread_rwlock_rdlock(rwlock_);
        int current = shm_->record_count;
        pthread_rwlock_unlock(rwlock_);

        int persisted = 0;
        while (lastSeen_ < current) {
            int idx = lastSeen_ % SHM_MAX_RECORDS;

            pthread_rwlock_rdlock(rwlock_);
            parking_record_t rec = shm_->records[idx];
            pthread_rwlock_unlock(rwlock_);

            if (rec.complete) {
                db_.insertRecord(rec);
                ++persisted;
            }
            ++lastSeen_;
        }
        return persisted;
    }

private:
    Database         &db_;
    shared_data_t    *shm_;
    pthread_rwlock_t *rwlock_;
    int               lastSeen_ = 0;
};
