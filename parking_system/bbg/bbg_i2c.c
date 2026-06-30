/**
 * bbg_i2c.c  —  BBG Process 2: I2C Master → Named PIPE
 *
 * RESPONSIBILITY:
 *   • Act as I2C master, read 48-byte GPS frames from STM32 slave (addr 0x08)
 *   • Parse frame and convert to wire format: "TYPE|ID|LAT,LON|CITY\n"
 *   • Write wire messages to named PIPE (/tmp/parking.pipe)
 *
 * SYNCHRONIZATION with Process 1:
 *   • Creates FIFO (blocks until Process 1 opens read end)
 *   • Writes parking events; Process 1 reads and forwards to TCP server
 *
 * BUILD & RUN:
 *   gcc bbg_i2c.c -o bbg_i2c
 *   sudo ./bbg_i2c
 *
 * IMPORTANT: Start this BEFORE bbg_tcp (it creates the FIFO)
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
#define TARGET_SLAVE_ADDR  0x08          /* STM32 slave address */
#define I2C_DEV            "/dev/i2c-2"  /* BBG I2C2 bus */
#define FRAME_SIZE         48            /* exact bytes to read from STM32 */
#define PIPE_PATH          "/tmp/parking.pipe"  /* IPC with Process 1 */

/* ── Frame Structure (must match STM32 gps_frame_t exactly) ── */
typedef struct {
    char type;             /* 'S' (start) or 'E' (end) parking  [byte 0]     */
    char customer_id[15];  /* e.g. "CAR-001\0"                  [bytes 1-15]  */
    char latitude[8];      /* e.g. "32.0853\0"                  [bytes 16-23] */
    char longitude[8];     /* e.g. "34.7817\0"                  [bytes 24-31] */
    char city[16];         /* e.g. "TelAviv\0"                  [bytes 32-47] */
} gps_frame_t;             /* TOTAL: 48 bytes (packed struct)               */

/* ── Global Variables ─────────────────────────────── */
static volatile int g_running = 1;  /* set to 0 by signal handler to exit loop */
static int          g_i2c_fd  = -1; /* file descriptor for I2C device */
static int          g_pipe_fd = -1; /* file descriptor for named PIPE write end */

/* ── Signal Handler ──────────────────────────────── */
/**
 * Gracefully shutdown on SIGINT (Ctrl+C) or SIGTERM
 */
static void handle_signal(int s)
{
    (void)s;  /* unused parameter */
    g_running = 0;  /* break out of main loop */
}

/* ── Open I2C Bus ──────────────────────────────── */
/**
 * Initialize I2C device as master
 * Returns: file descriptor on success, -1 on error
 */
static int i2c_open(void)
{
    /* Open /dev/i2c-2 for read/write */
    int fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        printf("[ERR] Cannot open %s: %s\n", I2C_DEV, strerror(errno));
        return -1;
    }

    /* Configure as I2C master communicating with slave at TARGET_SLAVE_ADDR */
    if (ioctl(fd, I2C_SLAVE, TARGET_SLAVE_ADDR) < 0) {
        printf("[ERR] ioctl I2C_SLAVE: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    printf("[I2C] Opened %s, slave=0x%02X\n", I2C_DEV, TARGET_SLAVE_ADDR);
    return fd;
}

/* ── Create & Open Named PIPE ──────────────────────────── */
/**
 * Create FIFO if needed, then open write end
 * Blocks until Process 1 (bbg_tcp) opens the read end
 * Returns: file descriptor on success, -1 on error
 */
static int pipe_open(void)
{
    /* Create FIFO with 0666 permissions (readable/writable by all) */
    if (mkfifo(PIPE_PATH, 0666) < 0 && errno != EEXIST) {
        printf("[ERR] mkfifo(%s): %s\n", PIPE_PATH, strerror(errno));
        return -1;
    }

    printf("[PIPE] FIFO created: %s\n", PIPE_PATH);
    printf("[PIPE] Waiting for Process 1 (bbg_tcp) to connect...\n");

    /* Open write end — BLOCKS until someone opens the read end */
    int fd = open(PIPE_PATH, O_WRONLY);
    if (fd < 0) {
        printf("[ERR] open pipe write end: %s\n", strerror(errno));
        return -1;
    }

    printf("[PIPE] Process 1 connected — pipe ready\n");
    return fd;
}

/* ── Main Loop ────────────────────────────────── */
int main(void)
{
    /* Register signal handlers for graceful shutdown */
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    /* Initialize I2C master */
    g_i2c_fd = i2c_open();
    if (g_i2c_fd < 0) return 1;

    /* Create FIFO and wait for Process 1 to connect (blocks here) */
    g_pipe_fd = pipe_open();
    if (g_pipe_fd < 0) {
        close(g_i2c_fd);
        return 1;
    }

    printf("=== BBG Process 2: I2C→PIPE started ===\n\n");

    /* Main event loop */
    while (g_running) {

        /* ── Step 1: Read 48-byte frame from STM32 via I2C ──────── */
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

        /* ── Step 2: Validate frame and ensure null-termination ──────── */
        gps_frame_t *f = (gps_frame_t *)buf;

        /* Safety: force null termination to prevent buffer overruns */
        f->customer_id[14] = '\0';
        f->latitude[7]     = '\0';
        f->longitude[7]    = '\0';
        f->city[15]        = '\0';

        /* Verify frame type is 'S' (start) or 'E' (end) */
        if (f->type != 'S' && f->type != 'E') {
            printf("[WARN] Bad frame type: 0x%02X\n",
                   (unsigned char)f->type);
            sleep(1);
            continue;
        }

        printf("[I2C] type=%c id=%s lat=%s lon=%s city=%s\n",
               f->type, f->customer_id,
               f->latitude, f->longitude, f->city);

        /* ── Step 3: Format into wire protocol ─────────────── */
        /* Wire format: "S|CAR-001|32.0853,34.7817|TelAviv\n" */
        char wire[128];
        int  wlen = snprintf(wire, sizeof(wire),
                             "%c|%s|%s,%s|%s\n",
                             f->type,
                             f->customer_id,
                             f->latitude,
                             f->longitude,
                             f->city);

        /* ── Step 4: Write formatted message to PIPE ─────────── */
        if (write(g_pipe_fd, wire, (size_t)wlen) < 0) {
            printf("[ERR] Pipe write: %s\n", strerror(errno));
            sleep(1);
            continue;
        }

        printf("[PIPE→] %s", wire);
        fflush(stdout);

        sleep(1);  /* Wait 1 second before next frame (STM32 sends 1/sec) */
    }

    /* Cleanup */
    if (g_i2c_fd  >= 0) close(g_i2c_fd);
    if (g_pipe_fd >= 0) close(g_pipe_fd);
    printf("\n[INFO] Process 2 stopped\n");
    return 0;
}
