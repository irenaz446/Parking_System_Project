/**
 * @file TcpServer.cpp
 * @brief Implementation of the poll()-based TCP server.
 *
 * Message flow
 * ─────────────
 *  1. poll() wakes on POLLIN for the listening fd  → acceptClient()
 *  2. poll() wakes on POLLIN for a client fd       → handleClient()
 *  3. handleClient() accumulates bytes until '\n', then calls
 *     MessageParser::parse() and processMessage().
 *  4. processMessage():
 *       MSG_START → SessionManager::start()
 *       MSG_END   → SessionManager::end(), fee = elapsed * price,
 *                   storeRecord() writes to shared memory.
 */

#include "TcpServer.hpp"
#include "../common/Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <vector>
#include <algorithm>

/* ── Constructor / Destructor ────────────────────────────────────────────── */

TcpServer::TcpServer(SharedMemory &shm,
                     pthread_rwlock_t *rwlock,
                     int port)
    : shm_(shm)
    , rwlock_(rwlock)
    , port_(port)
    , prices_(shm.data(), rwlock)
{
    if (!setupListenSocket())
        throw std::runtime_error("Failed to set up listen socket");
}

TcpServer::~TcpServer()
{
    if (listenFd_ >= 0) close(listenFd_);
}

/* ── Socket setup ────────────────────────────────────────────────────────── */

bool TcpServer::setupListenSocket()
{
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        Logger::error("socket(): " + std::string(strerror(errno)));
        return false;
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        Logger::error("bind(): " + std::string(strerror(errno)));
        return false;
    }

    if (listen(listenFd_, SOMAXCONN) < 0) {
        Logger::error("listen(): " + std::string(strerror(errno)));
        return false;
    }

    Logger::info("Listening on port " + std::to_string(port_));
    return true;
}

/* ── Main event loop ─────────────────────────────────────────────────────── */

void TcpServer::run()
{
    running_ = true;

    // fds[0] = listen socket; fds[1..] = connected clients
    std::vector<pollfd> fds;
    fds.push_back({listenFd_, POLLIN, 0});

    while (running_) {
        int ret = poll(fds.data(), static_cast<nfds_t>(fds.size()), 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            Logger::error("poll(): " + std::string(strerror(errno)));
            break;
        }

        // Check listening socket
        if (fds[0].revents & POLLIN)
            acceptClient();

        // Check client sockets (iterate backwards so erase() is safe)
        for (int i = static_cast<int>(fds.size()) - 1; i >= 1; --i) {
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                handleClient(fds[i].fd);
                // if handleClient disconnected the client, remove from fds
                if (recvBufs_.find(fds[i].fd) == recvBufs_.end())
                    fds.erase(fds.begin() + i);
            }
        }

        // Re-register any new clients added during acceptClient()
        // (we just rebuild the vector from recvBufs_ keys each iteration
        //  if the listen socket fired; for simplicity, acceptClient adds
        //  directly – see below)
        // The vector is updated inline in acceptClient().
        // We pass it by reference so it is mutated there.
        // Achieved via a lambda capture workaround: store pending fds.
        while (!pendingFds_.empty()) {
            fds.push_back({pendingFds_.back(), POLLIN, 0});
            pendingFds_.pop_back();
        }
    }

    // Close all client sockets
    for (std::size_t i = 1; i < fds.size(); ++i)
        close(fds[i].fd);

    Logger::info("Server event loop exited");
}

/* ── Accept ──────────────────────────────────────────────────────────────── */

void TcpServer::acceptClient()
{
    sockaddr_in caddr{};
    socklen_t   clen = sizeof(caddr);

    int cfd = accept(listenFd_,
                     reinterpret_cast<sockaddr *>(&caddr), &clen);
    if (cfd < 0) {
        Logger::warn("accept(): " + std::string(strerror(errno)));
        return;
    }

    if (recvBufs_.size() >= static_cast<std::size_t>(MAX_CLIENTS)) {
        Logger::warn("Max clients reached, rejecting connection");
        close(cfd);
        return;
    }

    recvBufs_[cfd] = "";          // empty receive buffer
    pendingFds_.push_back(cfd);   // queued for poll registration

    Logger::info("Client connected fd=" + std::to_string(cfd)
                 + " from " + inet_ntoa(caddr.sin_addr));
}

/* ── Receive and parse ───────────────────────────────────────────────────── */

void TcpServer::handleClient(int fd)
{
    char buf[BUF_SIZE];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);

    if (n <= 0) {
        Logger::info("Client fd=" + std::to_string(fd) + " disconnected");
        disconnectClient(fd);
        return;
    }

    buf[n] = '\0';
    recvBufs_[fd] += buf;     // append to partial-message buffer

    // Process every complete (newline-terminated) message in the buffer
    std::string &rbuf = recvBufs_[fd];
    std::size_t  pos;
    while ((pos = rbuf.find('\n')) != std::string::npos) {
        std::string line = rbuf.substr(0, pos);
        rbuf.erase(0, pos + 1);

        // Strip carriage-return if present
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) continue;

        auto msg = MessageParser::parse(line);
        if (msg.has_value()) {
            processMessage(fd, *msg);
        } else {
            Logger::warn("Bad message from fd=" + std::to_string(fd)
                         + ": [" + line + "]");
            sendReply(fd, "ERR:bad_msg\n");
        }
    }
}

/* ── Business logic ──────────────────────────────────────────────────────── */

void TcpServer::processMessage(int fd, const wire_msg_t &msg)
{
    const std::string customerId(msg.customer_id);
    const std::string city(msg.city);

    if (msg.type == MSG_START) {
        Session s(customerId, msg.lat, msg.lon, city, std::time(nullptr));
        sessions_.start(s);

        Logger::info("START id=" + customerId
                     + " city=" + city
                     + " lat=" + std::to_string(msg.lat)
                     + " lon=" + std::to_string(msg.lon));
        sendReply(fd, "OK:started\n");

    } else { /* MSG_END */
        auto session = sessions_.end(customerId);
        if (!session.has_value()) {
            Logger::warn("END without matching START for id=" + customerId);
            sendReply(fd, "ERR:no_session\n");
            return;
        }

        double elapsedMin = session->elapsedMinutes();
        double pricePerMin = prices_.lookup(session->city());
        if (pricePerMin < 0.0) pricePerMin = 0.0; // unknown city → no charge

        double fee = elapsedMin * pricePerMin;

        // Build the parking record
        parking_record_t rec{};
        strncpy(rec.customer_id, customerId.c_str(), CUSTOMER_ID_LEN - 1);
        rec.start_lat  = session->startLat();
        rec.start_lon  = session->startLon();
        rec.end_lat    = msg.lat;
        rec.end_lon    = msg.lon;
        rec.start_time = session->startTime();
        rec.end_time   = std::time(nullptr);
        rec.total_fee  = fee;
        strncpy(rec.city, session->city().c_str(), CITY_NAME_LEN - 1);
        rec.complete = 1;

        storeRecord(rec);

        char reply[128];
        std::snprintf(reply, sizeof(reply),
                      "OK:fee=%.2f:elapsed=%.1fmin\n", fee, elapsedMin);
        sendReply(fd, reply);

        Logger::info("END id=" + customerId
                     + " city=" + session->city()
                     + " elapsed=" + std::to_string(elapsedMin) + "min"
                     + " fee=" + std::to_string(fee));
    }
}

/* ── Shared memory write ─────────────────────────────────────────────────── */

void TcpServer::storeRecord(const parking_record_t &rec)
{
    pthread_rwlock_wrlock(rwlock_);

    shared_data_t *shm = shm_.data();
    int idx = shm->record_count % SHM_MAX_RECORDS;
    shm->records[idx] = rec;
    shm->record_count++;

    pthread_rwlock_unlock(rwlock_);

    Logger::info("Record stored: id=" + std::string(rec.customer_id)
                 + " fee=" + std::to_string(rec.total_fee));
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

void TcpServer::sendReply(int fd, const std::string &reply)
{
    send(fd, reply.c_str(), reply.size(), MSG_NOSIGNAL);
}

void TcpServer::disconnectClient(int fd)
{
    close(fd);
    recvBufs_.erase(fd);
}
