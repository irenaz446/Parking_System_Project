/**
 * bbg_tcp.c  —  BBG Process 1
 *
 * Reads wire messages from the named PIPE (written by bbg_i2c.c),
 * and forwards them to the TCP server over Ethernet.
 * Reconnects to the server automatically if the connection drops.
 *
 * Compile:  gcc bbg_tcp.c -o bbg_tcp
 * Run:      ./bbg_tcp <SERVER_IP> <SERVER_PORT>
 *
 * Start AFTER bbg_i2c is already waiting (so the FIFO exists).
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

/* ── Configuration ───────────────────────────────── */
#define PIPE_PATH          "/tmp/parking.pipe"
#define RECONNECT_DELAY_S  5
#define BUF_SIZE           256

/* ── Globals ─────────────────────────────────────── */
static volatile int g_running = 1;
static int          g_pipe_fd = -1;
static int          g_tcp_fd  = -1;

/* ── Signal handler ──────────────────────────────── */
static void handle_signal(int s)
{
    (void)s;
    g_running = 0;
}

/* ── Open PIPE read end ──────────────────────────── */
static int pipe_open(void)
{
    /* Opening the read end unblocks Process 2 which is waiting on write end */
    int fd = open(PIPE_PATH, O_RDONLY);
    if (fd < 0) {
        printf("[ERR] Cannot open pipe %s: %s\n",
               PIPE_PATH, strerror(errno));
        printf("[ERR] Is bbg_i2c running?\n");
        return -1;
    }
    printf("[PIPE] Opened %s for reading\n", PIPE_PATH);
    return fd;
}

/* ── Connect to TCP server ───────────────────────── */
static int tcp_connect(const char *ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("[ERR] socket(): %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        printf("[ERR] Invalid server IP: %s\n", ip);
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[ERR] connect(%s:%d): %s\n", ip, port, strerror(errno));
        close(fd);
        return -1;
    }

    printf("[TCP] Connected to server %s:%d\n", ip, port);
    return fd;
}

/* ── Send message, reconnect on failure ──────────── */
static int tcp_send(const char *ip, int port,
                    const char *msg, size_t len)
{
    /* Connect if not already connected */
    if (g_tcp_fd < 0) {
        g_tcp_fd = tcp_connect(ip, port);
        if (g_tcp_fd < 0) return -1;
    }

    if (send(g_tcp_fd, msg, len, MSG_NOSIGNAL) < 0) {
        printf("[ERR] TCP send: %s — reconnecting\n", strerror(errno));
        close(g_tcp_fd);
        g_tcp_fd = -1;
        return -1;
    }

    /* Read server reply non-blocking */
    char reply[128];
    ssize_t n = recv(g_tcp_fd, reply, sizeof(reply) - 1, MSG_DONTWAIT);
    if (n > 0) {
        reply[n] = '\0';
        reply[strcspn(reply, "\r\n")] = '\0';
        printf("[TCP] Server reply: %s\n", reply);
    }

    return 0;
}

/* ── Main ────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <SERVER_IP> <SERVER_PORT>\n", argv[0]);
        printf("  e.g. %s 192.168.1.100 8080\n", argv[0]);
        return 1;
    }

    const char *server_ip   = argv[1];
    int         server_port = atoi(argv[2]);

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);  /* don't crash on broken TCP connection */

    /* Open pipe read end — this unblocks Process 2 */
    g_pipe_fd = pipe_open();
    if (g_pipe_fd < 0) return 1;

    printf("=== BBG Process 1: PIPE→TCP started ===\n");
    printf("Server: %s:%d\n\n", server_ip, server_port);

    /* Receive buffer — accumulates data between reads */
    char   buf[BUF_SIZE];
    size_t buf_len = 0;

    while (g_running) {

        /* ── 1. Read from PIPE ──────────────────────── */
        ssize_t n = read(g_pipe_fd,
                         buf + buf_len,
                         sizeof(buf) - buf_len - 1);
        if (n < 0) {
            if (errno == EINTR) continue;   /* interrupted by signal */
            printf("[ERR] Pipe read: %s\n", strerror(errno));
            break;
        }
        if (n == 0) {
            /* Process 2 closed the pipe — wait for it to restart */
            printf("[WARN] Pipe closed by Process 2 — waiting...\n");
            close(g_pipe_fd);
            sleep(2);
            g_pipe_fd = pipe_open();
            if (g_pipe_fd < 0) break;
            buf_len = 0;
            continue;
        }

        buf_len += (size_t)n;
        buf[buf_len] = '\0';

        /* ── 2. Extract complete lines (ending with \n) */
        char *line  = buf;
        char *newline;

        while ((newline = strchr(line, '\n')) != NULL) {
            /* Null-terminate this line */
            *newline = '\0';
            size_t line_len = (size_t)(newline - line);

            printf("[←PIPE] %s\n", line);

            /* Restore newline for TCP message */
            *newline = '\n';

            /* ── 3. Forward complete line to TCP server */
            if (tcp_send(server_ip, server_port,
                         line, line_len + 1) < 0) {
                printf("[WARN] TCP failed — retry in %ds\n",
                       RECONNECT_DELAY_S);
                sleep(RECONNECT_DELAY_S);
            }

            line = newline + 1;
        }

        /* Move any incomplete line to front of buffer */
        buf_len = strlen(line);
        if (buf_len > 0 && line != buf)
            memmove(buf, line, buf_len);
    }

    if (g_pipe_fd >= 0) close(g_pipe_fd);
    if (g_tcp_fd  >= 0) close(g_tcp_fd);
    printf("\n[INFO] Process 1 stopped\n");
    return 0;
}
