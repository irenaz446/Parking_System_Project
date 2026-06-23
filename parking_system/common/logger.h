/**
 * @file logger.h
 * @brief Thread-safe timestamped logger for C components (bbg, price_updater).
 *
 * Usage:
 *   logger_init("/tmp/parking_logs/bbg_i2c.log");
 *   LOG_INFO("Process started");
 *   LOG_ERR("read failed: %s", strerror(errno));
 *   logger_close();
 */

#ifndef PARKING_LOGGER_H
#define PARKING_LOGGER_H

typedef enum { LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR } log_level_t;

/**
 * @brief Open (append) the log file.  Call once at startup.
 * @return 0 on success, -1 on failure.
 */
int  logger_init(const char *path);

/** @brief Write a formatted log entry. */
void log_msg(log_level_t level, const char *fmt, ...);

/** @brief Close the log file. */
void logger_close(void);

/* Convenience macros */
#define LOG_INFO(...)  log_msg(LOG_LEVEL_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  log_msg(LOG_LEVEL_WARN,  __VA_ARGS__)
#define LOG_ERR(...)   log_msg(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif /* PARKING_LOGGER_H */
