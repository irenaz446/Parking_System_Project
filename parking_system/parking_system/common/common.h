/**
 * @file common.h
 * @brief Shared definitions for every component of the parking system.
 *
 * This header is included by both C (bbg, stm32, price_updater) and C++
 * (server, db) translation units.  No C++-only syntax is used here so
 * that the C components can include it without a C++ compiler.
 *
 * All structs use plain C layout; the extern "C" guard makes them
 * link-compatible when included from C++ files.
 */

#ifndef PARKING_COMMON_H
#define PARKING_COMMON_H

#include <stdint.h>
#include <time.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── IPC ─────────────────────────────────────────────────────────────────── */
#define SHM_KEY          0x1234ABCD   /**< shmget() key                      */
#define SHM_MAX_CITIES   64           /**< Max city rows in shared memory     */
#define SHM_MAX_RECORDS  256          /**< Ring-buffer size for session records*/

/* ── Network ─────────────────────────────────────────────────────────────── */
#define DEFAULT_PORT     8080
#define MAX_CLIENTS      64
#define BUF_SIZE         512

/* ── Wire protocol type bytes ────────────────────────────────────────────── */
#define MSG_START  'S'
#define MSG_END    'E'

/* ── Signals ─────────────────────────────────────────────────────────────── */
#define SIG_PRICE_UPDATE  SIGUSR1   /**< Sent to DB to trigger price reload   */

/* ── Default paths (overridden by config file) ───────────────────────────── */
#define DEFAULT_PRICES_FILE  "config/prices.txt"
#define DEFAULT_LOG_DIR      "/tmp/parking_logs"
#define DEFAULT_DB_PATH      "parking.db"
#define DEFAULT_PID_FILE     "/tmp/parking_db.pid"

/* ── Field sizes ─────────────────────────────────────────────────────────── */
#define CITY_NAME_LEN    64
#define CUSTOMER_ID_LEN  64

/* ─────────────────────────────────────────────────────────────────────────
 * Plain-C structs (shared between C and C++ components via shared memory)
 * ───────────────────────────────────────────────────────────────────────── */

/** @brief One row in the city-pricing table (lives in shared memory). */
typedef struct {
    char   city[CITY_NAME_LEN];
    double price_per_minute;
    int    active;            /**< 1 = valid entry, 0 = logically deleted */
} city_price_t;

/** @brief A completed parking session (written by server, read by DB). */
typedef struct {
    char   customer_id[CUSTOMER_ID_LEN];
    double start_lat;
    double start_lon;
    double end_lat;
    double end_lon;
    time_t start_time;
    time_t end_time;
    double total_fee;
    char   city[CITY_NAME_LEN];
    int    complete;          /**< Set to 1 when end message is processed   */
} parking_record_t;

/** @brief The entire shared memory segment layout. */
typedef struct {
    /* Pricing table – written by DB, read by server ----------------------- */
    int          price_count;
    city_price_t prices[SHM_MAX_CITIES];

    /* Completed session ring-buffer – written by server, read by DB ------- */
    int              record_count;
    parking_record_t records[SHM_MAX_RECORDS];
} shared_data_t;

/**
 * @brief Decoded wire message from a TCP client.
 *
 * Wire format (newline-terminated):
 *   "<S|E>|<CUSTOMER_ID>|<LAT>,<LON>|<CITY>"
 */
typedef struct {
    char   type;                       /**< MSG_START ('S') or MSG_END ('E') */
    char   customer_id[CUSTOMER_ID_LEN];
    double lat;
    double lon;
    char   city[CITY_NAME_LEN];
} wire_msg_t;

#ifdef __cplusplus
}
#endif

#endif /* PARKING_COMMON_H */
