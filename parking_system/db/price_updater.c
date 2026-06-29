/**
 * @file price_updater.c
 * @brief Terminal CLI for adding, removing, or listing city prices — C.
 *
 * Usage:
 *   price_updater add    <CITY> <PRICE/MIN>  [PRICES_FILE] [PID_FILE]
 *   price_updater remove <CITY>              [PRICES_FILE] [PID_FILE]
 *   price_updater list                       [PRICES_FILE]
 *
 * After modifying the prices file the tool sends SIGUSR1 to the DB
 * process (whose PID it reads from the PID file) so that the DB reloads
 * the file and syncs the new prices to shared memory immediately.
 *
 * Default paths:
 *   PRICES_FILE = config/prices.txt
 *   PID_FILE    = /tmp/parking_db.pid
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define DEFAULT_PRICES_FILE  "config/prices.txt"
#define DEFAULT_PID_FILE     "/tmp/parking_db.pid"
#define MAX_LINE             256

/* ── Signal the DB process ────────────────────────────────────────────────── */

static void signal_db(const char *pid_file)
{
    FILE *fp = fopen(pid_file, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open PID file '%s': %s\n",
                pid_file, strerror(errno));
        return;
    }
    int pid = 0;
    if (fscanf(fp, "%d", &pid) != 1) pid = -1;
    fclose(fp);

    if (pid <= 0) {
        fprintf(stderr, "Invalid PID in '%s' – is the DB running?\n", pid_file);
        return;
    }
    if (kill((pid_t)pid, SIGUSR1) == 0)
        printf("Sent SIGUSR1 to DB process (pid=%d)\n", pid);
    else
        fprintf(stderr, "kill(%d, SIGUSR1): %s\n", pid, strerror(errno));
}

/* ── add command ─────────────────────────────────────────────────────────── */

static int cmd_add(const char *prices_file,
                   const char *city,
                   double      price)
{
    /* Rewrite the file, replacing the city's old entry if it existed */
    char tmp_path[MAX_LINE];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", prices_file);

    FILE *tmp = fopen(tmp_path, "w");
    if (!tmp) {
        fprintf(stderr, "Cannot create temp file: %s\n", strerror(errno));
        return -1;
    }

    FILE *fp = fopen(prices_file, "r");
    if (fp) {
        char line[MAX_LINE];
        while (fgets(line, sizeof(line), fp)) {
            char copy[MAX_LINE];
            snprintf(copy, MAX_LINE, "%s", line);
            copy[strcspn(copy, "\r\n")] = '\0';
            char *comma = strchr(copy, ',');
            if (comma) {
                *comma = '\0';
                if (strcmp(copy, city) == 0) continue; /* skip old entry */
            }
            fputs(line, tmp);
        }
        fclose(fp);
    }

    fprintf(tmp, "%s,%.6f\n", city, price);
    fclose(tmp);

    if (rename(tmp_path, prices_file) != 0) {
        fprintf(stderr, "rename failed: %s\n", strerror(errno));
        return -1;
    }
    printf("Added/updated: %s = %.4f/min\n", city, price);
    return 0;
}

/* ── remove command ──────────────────────────────────────────────────────── */

static int cmd_remove(const char *prices_file, const char *city)
{
    FILE *fp = fopen(prices_file, "a");
    if (!fp) {
        fprintf(stderr, "Cannot open prices file: %s\n", strerror(errno));
        return -1;
    }
    /* A line starting with '-' tells the DB to delete that city */
    fprintf(fp, "-%s\n", city);
    fclose(fp);
    printf("Marked '%s' for removal\n", city);
    return 0;
}

/* ── list command ────────────────────────────────────────────────────────── */

static void cmd_list(const char *prices_file)
{
    FILE *fp = fopen(prices_file, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open '%s': %s\n",
                prices_file, strerror(errno));
        return;
    }
    printf("%-30s  %s\n", "City", "Price/min");
    printf("%-30s  %s\n",
           "──────────────────────────────",
           "──────────");
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0] || line[0] == '#') continue;
        if (line[0] == '-') {
            printf("  [REMOVED] %s\n", line + 1);
            continue;
        }
        char *comma = strchr(line, ',');
        if (comma) {
            *comma = '\0';
            printf("%-30s  %.4f\n", line, atof(comma + 1));
        }
    }
    fclose(fp);
}

/* ── main ────────────────────────────────────────────────────────────────── */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s add    <CITY> <PRICE/MIN> [PRICES_FILE] [PID_FILE]\n"
            "  %s remove <CITY>             [PRICES_FILE] [PID_FILE]\n"
            "  %s list                      [PRICES_FILE]\n",
            prog, prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return EXIT_FAILURE; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "list") == 0) {
        cmd_list((argc > 2) ? argv[2] : DEFAULT_PRICES_FILE);
        return EXIT_SUCCESS;
    }

    if (strcmp(cmd, "add") == 0) {
        if (argc < 4) { usage(argv[0]); return EXIT_FAILURE; }
        const char *city  = argv[2];
        double      price = atof(argv[3]);
        const char *pf    = (argc > 4) ? argv[4] : DEFAULT_PRICES_FILE;
        const char *pidf  = (argc > 5) ? argv[5] : DEFAULT_PID_FILE;
        if (cmd_add(pf, city, price) == 0) signal_db(pidf);
        return EXIT_SUCCESS;
    }

    if (strcmp(cmd, "remove") == 0) {
        if (argc < 3) { usage(argv[0]); return EXIT_FAILURE; }
        const char *city = argv[2];
        const char *pf   = (argc > 3) ? argv[3] : DEFAULT_PRICES_FILE;
        const char *pidf = (argc > 4) ? argv[4] : DEFAULT_PID_FILE;
        if (cmd_remove(pf, city) == 0) signal_db(pidf);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage(argv[0]);
    return EXIT_FAILURE;
}
