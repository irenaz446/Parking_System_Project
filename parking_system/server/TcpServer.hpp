/**
 * @file TcpServer.hpp
 * @brief Asynchronous TCP server class (poll-based, single-threaded I/O loop).
 *
 * TcpServer owns the listening socket and the client file-descriptor table.
 * It delegates message semantics to SessionManager, PriceTable, and the
 * shared memory segment.
 *
 * Design choices
 * ──────────────
 * • poll() over select(): no fd_set size limitation, easier to manage.
 * • Single I/O thread: parking-fee calculation is O(1) so there is no
 *   need for a thread pool; adding one later is straightforward.
 * • Per-client receive buffer stored in a std::unordered_map so that
 *   partial messages across multiple recv() calls are handled correctly.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <pthread.h>

#include "../common/common.h"
#include "../common/SharedMemory.hpp"
#include "SessionManager.hpp"
#include "PriceTable.hpp"
#include "MessageParser.hpp"

class TcpServer {
public:
    /**
     * @param shm      Shared memory (already attached).
     * @param rwlock   Inter-process rwlock protecting shm.
     * @param port     TCP port to listen on.
     */
    TcpServer(SharedMemory &shm,
              pthread_rwlock_t *rwlock,
              int port);

    ~TcpServer();

    /* Non-copyable */
    TcpServer(const TcpServer &)            = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    /** @brief Enter the poll() event loop.  Returns when stop() is called. */
    void run();

    /** @brief Signal the event loop to exit cleanly (signal-handler safe). */
    void stop() { running_ = false; }

private:
    /* ── helpers ──────────────────────────────────────────────────────── */
    bool setupListenSocket();
    void acceptClient();
    void handleClient(int fd);
    void processMessage(int fd, const wire_msg_t &msg);
    void sendReply(int fd, const std::string &reply);
    void disconnectClient(int fd);
    void storeRecord(const parking_record_t &rec);

    /* ── state ────────────────────────────────────────────────────────── */
    SharedMemory     &shm_;
    pthread_rwlock_t *rwlock_;
    int               port_;
    int               listenFd_  = -1;
    std::atomic<bool> running_   {false};

    SessionManager    sessions_;
    PriceTable        prices_;

    /* Per-client partial receive buffers */
    std::unordered_map<int, std::string> recvBufs_;

    /* New fds queued by acceptClient(), registered in next poll() pass */
    std::vector<int> pendingFds_;
};
