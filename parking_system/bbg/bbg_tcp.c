/**
 * bbg_tcp.c  —  BBG Process 1: Named PIPE → TCP Server
 *
 * RESPONSIBILITY:
 *   • Read wire messages from named PIPE (/tmp/parking.pipe)
 *   • Forward complete lines to TCP server on PC
 *   • Handle TCP reconnection on connection loss
 *   • Buffer incomplete lines until newline arrives
 *
 * SYNCHRONIZATION with Process 2:
 *   • Waits for Process 2 to create FIFO
 *   • Opens read end to unblock Process 2 (which waits on write end)
 *
 * BUILD & RUN:
 *   gcc bbg_tcp.c -o bbg_tcp
 *   ./bbg_tcp <SERVER_IP> <SERVER_PORT>
 *   Example: ./bbg_tcp 192.168.10.1 8080
 *
 * IMPORTANT: Start this AFTER bbg_i2c (it opens the FIFO read end)
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
#define PIPE_PATH          "/tmp/parking.pipe"  /* read from Process 2 */
#define RECONNECT_DELAY_S  5                    /* TCP retry delay */
#define BUF_SIZE           256                   /* max message buffer */

/* ── Global Variables ─────────────────────────────── */
static volatile int g_running = 1;  /* set to 0 by signal handler */
static int          g_pipe_fd = -1; /* file descriptor for PIPE read end */
static int          g_tcp_fd  = -1; /* file descriptor for TCP socket */

/* ── Signal Handler ──────────────────────────────– */
/**
 * Gracefully shutdown on SIGINT (Ctrl+C) or SIGTERM
 */
static void handle_signal(int s)
{
    (void)s;  /* unused parameter */
    g_running = 0;  /* break out of main loop */
}

/* ── Open PIPE Read End ──────────────────────────– */
/**
 * Open named PIPE for reading
 * Opening the read end unblocks Process 2 which waits on write end
 * Returns: file descriptor on success, -1 on error
 */
static int pipe_open(void)
{
    /* Open read end of FIFO (non-blocking would require more logic) */
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

/* ── Connect to TCP Server ──────────────────────– */
/**
 * Establish TCP connection to server
 * Returns: socket fd on success, -1 on error
 */
static int tcp_connect(const char *ip, int port)
{
    /* Create TCP socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("[ERR] socket(): %s\n", strerror(errno));
        return -1;
    }

    /* Setup server address structure */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    /* Parse IP address string */
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        printf("[ERR] Invalid server IP: %s\n", ip);
        close(fd);
        return -1;
    }

    /* Attempt connection */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[ERR] connect(%s:%d): %s\n", ip, port, strerror(errno));
        close(fd);
        return -1;
    }

    printf("[TCP] Connected to server %s:%d\n", ip, port);
    return fd;
}

/* ── Send Message to Server ──────────────────────– */
/**
 * Send wire message to TCP server
 * Reconnects automatically if connection is lost
 * Reads server reply non-blocking and logs it
 * Returns: 0 on success, -1 on failure
 */
static int tcp_send(const char *ip, int port,
                    const char *msg, size_t len)
{
    /* Connect if not already connected */
    if (g_tcp_fd < 0) {
        g_tcp_fd = tcp_connect(ip, port);
        if (g_tcp_fd < 0) return -1;
    }

    /* Send message (MSG_NOSIGNAL prevents crash on broken pipe) */
    if (send(g_tcp_fd, msg, len, MSG_NOSIGNAL) < 0) {
        printf("[ERR] TCP send: %s — reconnecting\n", strerror(errno));
        close(g_tcp_fd);
        g_tcp_fd = -1;
        return -1;
    }

    /* Try to read server reply (non-blocking) */
    char reply[128];
    ssize_t n = recv(g_tcp_fd, reply, sizeof(reply) - 1, MSG_DONTWAIT);
    if (n > 0) {
        reply[n] = '\0';
        /* Strip newline/carriage return from reply */
        reply[strcspn(reply, "\r\n")] = '\0';
        printf("[TCP] Server reply: %s\n", reply);
    }

    return 0;
}

/* ── Main Loop ──────────────────────────────────– */
int main(int argc, char *argv[])
{
    /* Validate command-line arguments */
    if (argc < 3) {
        printf("Usage: %s <SERVER_IP> <SERVER_PORT>\n", argv[0]);
        printf("  e.g. %s 192.168.1.100 8080\n", argv[0]);
        return 1;
    }

    const char *server_ip   = argv[1];
    int         server_port = atoi(argv[2]);

    /* Register signal handlers for graceful shutdown */
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);  /* prevent crash when TCP peer closes */

    /* Open PIPE read end — this unblocks Process 2 */
    g_pipe_fd = pipe_open();
    if (g_pipe_fd < 0) return 1;

    printf("=== BBG Process 1: PIPE→TCP started ===\n");
    printf("Server: %s:%d\n\n", server_ip, server_port);

    /* Receive buffer — accumulates data between reads */
    char   buf[BUF_SIZE];
    size_t buf_len = 0;

    /* Main event loop */
    while (g_running) {

        /* ── Step 1: Read from PIPE ──────────────────────────── */
        ssize_t n = read(g_pipe_fd,
                         buf + buf_len,              /* append to end */
                         sizeof(buf) - buf_len - 1); /* space left */
        if (n < 0) {
            if (errno == EINTR) continue;   /* interrupted by signal */
            printf("[ERR] Pipe read: %s\n", strerror(errno));
            break;
        }
        if (n == 0) {
            /* Process 2 closed the pipe — wait for restart */
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

        /* ── Step 2: Extract complete lines (ending with \n) ──────── */
        char *line  = buf;
        char *newline;

        /* Process all complete lines in buffer */
        while ((newline = strchr(line, '\n')) != NULL) {
            /* Temporarily null-terminate this line */
            *newline = '\0';
            size_t line_len = (size_t)(newline - line);

            printf("[←PIPE] %s\n", line);

            /* Restore newline for TCP message */
            *newline = '\n';

            /* ── Step 3: Forward complete line to TCP server ──────── */
            if (tcp_send(server_ip, server_port,
                         line, line_len + 1) < 0) {
                printf("[WARN] TCP failed — retry in %ds\n",
                       RECONNECT_DELAY_S);
                sleep(RECONNECT_DELAY_S);
            }

            line = newline + 1;
        }

        /* Move any incomplete line to front of buffer for next read */
        buf_len = strlen(line);
        if (buf_len > 0 && line != buf)
            memmove(buf, line, buf_len);
    }

    /* Cleanup */
    if (g_pipe_fd >= 0) close(g_pipe_fd);
    if (g_tcp_fd  >= 0) close(g_tcp_fd);
    printf("\n[INFO] Process 1 stopped\n");
    return 0;
}
