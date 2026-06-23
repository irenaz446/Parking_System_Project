/**
 * @file bbg_tcp.c
 * @brief BBG Process 1 — TCP client bridge (C).
 *
 * Opens the read end of the named FIFO written by Process 2 and forwards
 * every newline-terminated wire message to the TCP server over Ethernet.
 * Reconnects automatically if the server connection is lost.
 *
 * Usage:
 *   bbg_tcp <SERVER_IP> <SERVER_PORT> [PIPE_PATH] [LOG_FILE]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include "../common/common.h"
#include "../common/logger.h"

/* ── Globals ──────────────────────────────────────────────────────────────── */

static volatile int g_running   = 1;
static int          g_server_fd = -1;

static void sighandler(int s) { (void)s; g_running = 0; }

/* ── TCP helpers ──────────────────────────────────────────────────────────── */

static int tcp_connect(const char *ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { LOG_ERR("socket(): %s", strerror(errno)); return -1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        LOG_ERR("Invalid server IP: %s", ip);
        close(fd); return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_WARN("connect(%s:%d): %s", ip, port, strerror(errno));
        close(fd); return -1;
    }
    LOG_INFO("Connected to server %s:%d", ip, port);
    return fd;
}

static int tcp_send(int fd, const char *msg, size_t len)
{
    if (send(fd, msg, len, MSG_NOSIGNAL) < 0) {
        LOG_ERR("send(): %s", strerror(errno));
        return -1;
    }
    return 0;
}

static void tcp_read_reply(int fd)
{
    char buf[256];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    if (n > 0) { buf[n] = '\0'; LOG_INFO("Server reply: %s", buf); }
}

/* ── FIFO helper ─────────────────────────────────────────────────────────── */

static int pipe_open_read(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { LOG_ERR("open pipe '%s': %s", path, strerror(errno)); return -1; }
    LOG_INFO("Pipe opened for reading: %s", path);
    return fd;
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <SERVER_IP> <PORT> [PIPE] [LOG]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip   = argv[1];
    int         server_port = atoi(argv[2]);
    const char *pipe_path   = (argc > 3) ? argv[3] : "/tmp/parking.pipe";
    const char *log_file    = (argc > 4) ? argv[4] : "/tmp/parking_logs/bbg_tcp.log";

    mkdir("/tmp/parking_logs", 0755);
    logger_init(log_file);
    LOG_INFO("=== BBG TCP Process 1 (C) starting ===");
    LOG_INFO("Server: %s:%d  Pipe: %s", server_ip, server_port, pipe_path);

    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGPIPE, SIG_IGN);

    int pipe_fd = pipe_open_read(pipe_path);
    if (pipe_fd < 0) { logger_close(); return EXIT_FAILURE; }

    char   buf[BUF_SIZE];
    size_t buf_len = 0;

    while (g_running) {
        /* Ensure TCP connection is live */
        if (g_server_fd < 0) {
            g_server_fd = tcp_connect(server_ip, server_port);
            if (g_server_fd < 0) {
                LOG_WARN("Retrying server in 5s...");
                sleep(5);
                continue;
            }
        }

        ssize_t n = read(pipe_fd, buf + buf_len, sizeof(buf) - buf_len - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_ERR("pipe read: %s", strerror(errno));
            break;
        }
        if (n == 0) {
            /* Write end of FIFO closed (Process 2 exited) – reopen */
            LOG_WARN("Pipe closed – waiting for Process 2...");
            close(pipe_fd);
            sleep(2);
            pipe_fd = pipe_open_read(pipe_path);
            if (pipe_fd < 0) break;
            continue;
        }

        buf_len += (size_t)n;
        buf[buf_len] = '\0';

        /* Forward each complete line */
        char *line = buf;
        char *nl;
        while ((nl = strchr(line, '\n')) != NULL) {
            size_t line_len = (size_t)(nl - line) + 1;
            LOG_INFO("-> server: %.*s", (int)(line_len - 1), line);
            if (tcp_send(g_server_fd, line, line_len) < 0) {
                close(g_server_fd);
                g_server_fd = -1;
                break;
            }
            tcp_read_reply(g_server_fd);
            line += line_len;
        }

        /* Move partial line to front of buffer */
        buf_len = strlen(line);
        if (buf_len && line != buf)
            memmove(buf, line, buf_len);
    }

    if (g_server_fd >= 0) close(g_server_fd);
    close(pipe_fd);
    LOG_INFO("BBG TCP process stopped");
    logger_close();
    return EXIT_SUCCESS;
}
