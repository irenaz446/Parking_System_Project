/**
 * @file bbg_i2c.c
 * @brief BBG Process 2 — I2C receiver (C).
 *
 * Reads fixed-size frames from the STM32 via the Linux I2C character
 * device (/dev/i2c-N) and forwards them to Process 1 through a named
 * FIFO as newline-terminated wire messages.
 *
 * I2C frame layout (160 bytes, packed):
 *   [0]       type byte  ('S' or 'E')
 *   [1–64]    customer_id (null-terminated)
 *   [65–80]   latitude as ASCII string
 *   [81–96]   longitude as ASCII string
 *   [97–159]  city (null-terminated)
 *
 * Wire message written to PIPE:
 *   "<TYPE>|<ID>|<LAT>,<LON>|<CITY>\n"
 *
 * Usage:
 *   bbg_i2c <I2C_DEV> <I2C_ADDR_HEX> <PIPE_PATH> <LOG_FILE> [1=daemon]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

#ifdef __linux__
#  include <linux/i2c-dev.h>
#  include <sys/ioctl.h>
#endif

#ifndef I2C_SLAVE
#  define I2C_SLAVE 0x0703
#endif

#include "../common/common.h"
#include "../common/logger.h"

/* ── I2C frame struct (must match STM32 side) ──────────────────────────── */
#define I2C_FRAME_BYTES  160
#define POLL_MS          200

#pragma pack(push, 1)
typedef struct {
    char type;           /* 'S' or 'E'                offset   0 */
    char customer_id[64];/* null-terminated            offset   1 */
    char lat_str[16];    /* e.g. "32.085300\0"         offset  65 */
    char lon_str[16];    /* e.g. "34.781700\0"         offset  81 */
    char city[63];       /* null-terminated            offset  97 */
} i2c_frame_t;           /*                            total  160 */
#pragma pack(pop)

/* ── Module globals ──────────────────────────────────────────────────────── */
static volatile int g_running = 1;

static void sighandler(int s) { (void)s; g_running = 0; }

/* ── I2C helpers ─────────────────────────────────────────────────────────── */

static int i2c_open(const char *dev, int addr)
{
#ifdef __linux__
    int fd = open(dev, O_RDWR);
    if (fd < 0) { LOG_ERR("open('%s'): %s", dev, strerror(errno)); return -1; }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        LOG_ERR("ioctl(I2C_SLAVE,0x%02x): %s", addr, strerror(errno));
        close(fd); return -1;
    }
    LOG_INFO("I2C opened: %s addr=0x%02x", dev, addr);
    return fd;
#else
    (void)dev; (void)addr;
    LOG_WARN("I2C not supported – mock reads from /dev/null");
    return open("/dev/null", O_RDONLY);
#endif
}

/**
 * @brief Try to read one frame.
 * @return 1 = valid frame received, 0 = no data, -1 = error.
 */
static int i2c_read_frame(int fd, i2c_frame_t *frame)
{
    unsigned char buf[I2C_FRAME_BYTES];
    memset(buf, 0, sizeof(buf));

    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        LOG_ERR("I2C read: %s", strerror(errno));
        return -1;
    }
    if (n == 0) return 0;

    memcpy(frame, buf, sizeof(i2c_frame_t));

    if (frame->type != MSG_START && frame->type != MSG_END) {
        LOG_WARN("Invalid frame type byte: 0x%02x", (unsigned char)frame->type);
        return 0;
    }
    return 1;
}

/* ── FIFO helpers ─────────────────────────────────────────────────────────── */

static int pipe_open_write(const char *path)
{
    if (mkfifo(path, 0666) < 0 && errno != EEXIST) {
        LOG_ERR("mkfifo('%s'): %s", path, strerror(errno));
        return -1;
    }
    int fd = open(path, O_WRONLY);
    if (fd < 0) { LOG_ERR("open pipe '%s': %s", path, strerror(errno)); return -1; }
    LOG_INFO("Pipe opened for writing: %s", path);
    return fd;
}

static void pipe_write_frame(int fd, const i2c_frame_t *f)
{
    char msg[BUF_SIZE];
    int  len = snprintf(msg, sizeof(msg),
                        "%c|%s|%s,%s|%s\n",
                        f->type,
                        f->customer_id,
                        f->lat_str,
                        f->lon_str,
                        f->city);
    if (len > 0 && write(fd, msg, (size_t)len) < 0)
        LOG_ERR("pipe write: %s", strerror(errno));
    else
        LOG_INFO("-> pipe: %c|%s|%s,%s|%s",
                 f->type, f->customer_id,
                 f->lat_str, f->lon_str, f->city);
}

/* ── Daemon helper ─────────────────────────────────────────────────────────── */

static void daemonise(void)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) { perror("setsid"); exit(EXIT_FAILURE); }

    pid = fork();
    if (pid < 0) { perror("fork2"); exit(EXIT_FAILURE); }
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") != 0) perror("chdir");

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *i2c_dev   = (argc > 1) ? argv[1] : "/dev/i2c-1";
    int         i2c_addr  = (argc > 2) ? (int)strtol(argv[2], NULL, 0) : 0x08;
    const char *pipe_path = (argc > 3) ? argv[3] : "/tmp/parking.pipe";
    const char *log_file  = (argc > 4) ? argv[4] : "/tmp/parking_logs/bbg_i2c.log";
    int         as_daemon = (argc > 5) ? atoi(argv[5]) : 0;

    if (as_daemon) daemonise();

    mkdir("/tmp/parking_logs", 0755);
    logger_init(log_file);
    LOG_INFO("=== BBG I2C Process 2 (C) starting ===");

    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);

    int i2c_fd = i2c_open(i2c_dev, i2c_addr);
    if (i2c_fd < 0) { logger_close(); return EXIT_FAILURE; }

    int pipe_fd = pipe_open_write(pipe_path);
    if (pipe_fd < 0) { close(i2c_fd); logger_close(); return EXIT_FAILURE; }

    i2c_frame_t frame;
    while (g_running) {
        int rc = i2c_read_frame(i2c_fd, &frame);
        if (rc == 1) {
            pipe_write_frame(pipe_fd, &frame);
        } else if (rc < 0) {
            sleep(1); /* transient I2C error – wait and retry */
        }
        usleep(POLL_MS * 1000u);
    }

    close(i2c_fd);
    close(pipe_fd);
    LOG_INFO("BBG I2C process stopped");
    logger_close();
    return EXIT_SUCCESS;
}
