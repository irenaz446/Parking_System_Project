/**
 * bbg_i2c.c  —  BBG Process 2
 *
 * Reads 48-byte GPS frames from STM32 via I2C (master),
 * formats them as wire strings and writes to a named PIPE.
 *
 * Compile:  gcc bbg_i2c.c -o bbg_i2c
 * Run:      sudo ./bbg_i2c
 *
 * Start this BEFORE bbg_tcp — it creates the FIFO and blocks
 * until the read end is opened by Process 1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>

/* ── Configuration ───────────────────────────────── */
#define TARGET_SLAVE_ADDR  0x08
#define I2C_DEV            "/dev/i2c-2"
#define FRAME_SIZE         48
#define PIPE_PATH          "/tmp/parking.pipe"

/* ── Frame — must match STM32 gps_frame_t exactly ── */
typedef struct {
    char type;             /* 'S' or 'E'   [0]     */
    char customer_id[15];  /*               [1-15]  */
    char latitude[8];      /*               [16-23] */
    char longitude[8];     /*               [24-31] */
    char city[16];         /*               [32-47] */
} gps_frame_t;             /* total = 48 bytes      */

/* ── Globals ─────────────────────────────────────── */
static volatile int g_running = 1;
static int          g_i2c_fd  = -1;
static int          g_pipe_fd = -1;

/* ── Signal handler ──────────────────────────────── */
static void handle_signal(int s)
{
    (void)s;
    g_running = 0;
}

/* ── Open I2C bus as master ──────────────────────── */
static int i2c_open(void)
{
    int fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        printf("[ERR] Cannot open %s: %s\n", I2C_DEV, strerror(errno));
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, TARGET_SLAVE_ADDR) < 0) {
        printf("[ERR] ioctl I2C_SLAVE: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    printf("[I2C] Opened %s, slave=0x%02X\n", I2C_DEV, TARGET_SLAVE_ADDR);
    return fd;
}

/* ── Create FIFO and open write end ─────────────── */
static int pipe_open(void)
{
    /* Create FIFO if it does not exist yet */
    if (mkfifo(PIPE_PATH, 0666) < 0 && errno != EEXIST) {
        printf("[ERR] mkfifo(%s): %s\n", PIPE_PATH, strerror(errno));
        return -1;
    }

    /* Opening the write end blocks until Process 1 opens the read end */
    printf("[PIPE] FIFO created: %s\n", PIPE_PATH);
    printf("[PIPE] Waiting for Process 1 (bbg_tcp) to connect...\n");

    int fd = open(PIPE_PATH, O_WRONLY);
    if (fd < 0) {
        printf("[ERR] open pipe write end: %s\n", strerror(errno));
        return -1;
    }
    printf("[PIPE] Process 1 connected — pipe ready\n");
    return fd;
}

/* ── Main ────────────────────────────────────────── */
int main(void)
{
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    /* Open I2C bus */
    g_i2c_fd = i2c_open();
    if (g_i2c_fd < 0) return 1;

    /* Open pipe write end — blocks until Process 1 opens read end */
    g_pipe_fd = pipe_open();
    if (g_pipe_fd < 0) {
        close(g_i2c_fd);
        return 1;
    }

    printf("=== BBG Process 2: I2C→PIPE started ===\n\n");

    while (g_running) {

        /* ── 1. Read frame from STM32 via I2C ──────── */
        unsigned char buf[FRAME_SIZE];
        memset(buf, 0, sizeof(buf));

        int n = read(g_i2c_fd, buf, FRAME_SIZE);
        if (n < 0) {
            printf("[ERR] I2C read: %s\n", strerror(errno));
            sleep(1);
            continue;
        }
        if (n < FRAME_SIZE) {
            printf("[WARN] Short read: %d/%d bytes\n", n, FRAME_SIZE);
            sleep(1);
            continue;
        }

        /* ── 2. Validate frame ──────────────────────── */
        gps_frame_t *f = (gps_frame_t *)buf;

        /* Force null termination — safety against corrupt data */
        f->customer_id[14] = '\0';
        f->latitude[7]     = '\0';
        f->longitude[7]    = '\0';
        f->city[15]        = '\0';

        if (f->type != 'S' && f->type != 'E') {
            printf("[WARN] Bad frame type: 0x%02X\n",
                   (unsigned char)f->type);
            sleep(1);
            continue;
        }

        printf("[I2C] type=%c id=%s lat=%s lon=%s city=%s\n",
               f->type, f->customer_id,
               f->latitude, f->longitude, f->city);

        /* ── 3. Format wire message ─────────────────── */
        /* Format: "S|CAR-001|32.0853,34.7817|TelAviv\n" */
        char wire[128];
        int  wlen = snprintf(wire, sizeof(wire),
                             "%c|%s|%s,%s|%s\n",
                             f->type,
                             f->customer_id,
                             f->latitude,
                             f->longitude,
                             f->city);

        /* ── 4. Write wire message to PIPE ─────────── */
        if (write(g_pipe_fd, wire, (size_t)wlen) < 0) {
            printf("[ERR] Pipe write: %s\n", strerror(errno));
            sleep(1);
            continue;
        }

        printf("[PIPE→] %s", wire);
        fflush(stdout);

        sleep(1);
    }

    if (g_i2c_fd  >= 0) close(g_i2c_fd);
    if (g_pipe_fd >= 0) close(g_pipe_fd);
    printf("\n[INFO] Process 2 stopped\n");
    return 0;
}
