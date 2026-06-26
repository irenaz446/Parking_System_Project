/**
 * bbg_i2c_master_simple.c
 * BBG I2C Master — polls STM32 slave for 48-byte GPS frames.
 *
 * Compile:  gcc bbg_i2c_master_simple.c -o bbg_master
 * Run:      sudo ./bbg_master
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>          /* signal(), SIGINT, SIGTERM */
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/* ── Must match STM32 gps_frame_t exactly ────────── */
#define TARGET_SLAVE_ADDR  0x08
#define I2C_DEV            "/dev/i2c-2"
#define FRAME_SIZE         48           /* updated: now includes city field */

typedef struct {
    char type;             /* 'S' or 'E'   [0]     */
    char customer_id[15];  /* "CAR-001"    [1-15]  */
    char latitude[8];      /* "32.0853"    [16-23] */
    char longitude[8];     /* "34.7817"    [24-31] */
    char city[16];         /* "TelAviv"    [32-47] */
} gps_frame_t;             /* total = 48 bytes     */

/* ── Globals ─────────────────────────────────────── */
static volatile int g_running = 1;
static int          g_fd      = -1;

/* ── Signal handler — Ctrl-C closes fd cleanly ───── */
static void handle_signal(int s)
{
    (void)s;
    g_running = 0;
}

/* ── Entry point ─────────────────────────────────── */
int main(void)
{
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    /* Open I2C bus */
    g_fd = open(I2C_DEV, O_RDWR);
    if (g_fd < 0) {
        printf("ERROR: Cannot open %s: %s\n", I2C_DEV, strerror(errno));
        printf("Try:   sudo ./bbg_master\n");
        return 1;
    }

    /* Tell kernel which slave address we will talk to */
    if (ioctl(g_fd, I2C_SLAVE, TARGET_SLAVE_ADDR) < 0) {
        printf("ERROR: ioctl I2C_SLAVE: %s\n", strerror(errno));
        close(g_fd);
        return 1;
    }

    printf("=== BBG I2C Master ready ===\n");
    printf("Device      : %s\n",   I2C_DEV);
    printf("Slave addr  : 0x%02X\n", TARGET_SLAVE_ADDR);
    printf("Frame size  : %d bytes\n", FRAME_SIZE);
    printf("Polling STM32 every 1 second...\n\n");

    while (g_running) {
        unsigned char buf[FRAME_SIZE];
        memset(buf, 0, sizeof(buf));

        /* read() as master: drives SCL, requests FRAME_SIZE bytes from slave */
        int n = read(g_fd, buf, FRAME_SIZE);

        if (n < 0) {
            printf("[ERR] I2C read failed: %s\n", strerror(errno));
            sleep(1);
            continue;
        }
        if (n < FRAME_SIZE) {
            printf("[WARN] Short read: got %d/%d bytes\n", n, FRAME_SIZE);
            sleep(1);
            continue;
        }

        /* Overlay struct onto raw buffer */
        gps_frame_t *frame = (gps_frame_t *)buf;

        /* Force null-termination — safety against corrupt frames */
        frame->customer_id[14] = '\0';
        frame->latitude[7]     = '\0';
        frame->longitude[7]    = '\0';
        frame->city[15]        = '\0';

        /* Validate type byte */
        if (frame->type != 'S' && frame->type != 'E') {
            printf("[WARN] Bad type byte: 0x%02X\n",
                   (unsigned char)frame->type);
            sleep(1);
            continue;
        }

        /* Print received frame */
        printf("[RECV] type=%c | id=%s | lat=%s | lon=%s | city=%s\n",
               frame->type,
               frame->customer_id,
               frame->latitude,
               frame->longitude,
               frame->city);
        fflush(stdout);

        sleep(1);   /* poll once per second */
    }

    /* Clean shutdown */
    close(g_fd);
    printf("\n[INFO] Shutdown cleanly\n");
    return 0;
}
