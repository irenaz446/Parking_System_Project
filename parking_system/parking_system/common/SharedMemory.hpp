/**
 * @file SharedMemory.hpp
 * @brief RAII wrapper around the POSIX shared memory segment.
 *
 * Both the server and the DB instantiate one SharedMemory object.
 * The underlying OS segment persists until explicitly removed with
 * SharedMemory::destroy(), so both processes can attach independently.
 *
 * Thread / process safety: the caller is responsible for holding the
 * appropriate pthread_rwlock before reading or writing the data.
 */

#pragma once

#include <sys/shm.h>
#include <stdexcept>
#include <string>
#include <cstring>
#include <cerrno>

#include "common.h"

class SharedMemory {
public:
    /**
     * @brief Attach to (or create) the shared memory segment.
     * @throws std::runtime_error on shmget/shmat failure.
     */
    SharedMemory()
    {
        id_ = shmget(SHM_KEY, sizeof(shared_data_t), IPC_CREAT | 0666);
        if (id_ < 0)
            throw std::runtime_error("shmget failed: " + errstr());

        ptr_ = static_cast<shared_data_t *>(shmat(id_, nullptr, 0));
        if (ptr_ == reinterpret_cast<shared_data_t *>(-1)) {
            ptr_ = nullptr;
            throw std::runtime_error("shmat failed: " + errstr());
        }
    }

    /** @brief Detach on destruction (segment is NOT removed). */
    ~SharedMemory()
    {
        if (ptr_) shmdt(ptr_);
    }

    /* Non-copyable, movable */
    SharedMemory(const SharedMemory &)            = delete;
    SharedMemory &operator=(const SharedMemory &) = delete;

    SharedMemory(SharedMemory &&o) noexcept : id_(o.id_), ptr_(o.ptr_)
    {
        o.ptr_ = nullptr;
    }

    /** @brief Direct pointer to the shared data. Never null after construction. */
    shared_data_t *data() { return ptr_; }
    const shared_data_t *data() const { return ptr_; }

    /** @brief Remove the OS segment entirely (call from the last user). */
    void destroy()
    {
        if (id_ >= 0) shmctl(id_, IPC_RMID, nullptr);
    }

    int id() const { return id_; }

private:
    int            id_  = -1;
    shared_data_t *ptr_ = nullptr;

    static std::string errstr()
    {
        return std::string(strerror(errno));
    }
};
