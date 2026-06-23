/**
 * @file logger.c
 * @brief C logger implementation — thread-safe via pthread_mutex_t.
 */

#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

static FILE            *g_fp  = NULL;
static pthread_mutex_t  g_mx  = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str(log_level_t l)
{
    switch (l) {
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "?????";
    }
}

int logger_init(const char *path)
{
    pthread_mutex_lock(&g_mx);
    if (g_fp) fclose(g_fp);
    g_fp = fopen(path, "a");
    pthread_mutex_unlock(&g_mx);
    return g_fp ? 0 : -1;
}

void log_msg(log_level_t level, const char *fmt, ...)
{
    char    tsbuf[32];
    time_t  now = time(NULL);
    strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    va_list ap;
    va_start(ap, fmt);

    pthread_mutex_lock(&g_mx);
    FILE *out = g_fp ? g_fp : stderr;
    fprintf(out, "[%s] [%s] ", tsbuf, level_str(level));
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    fflush(out);
    pthread_mutex_unlock(&g_mx);

    va_end(ap);
}

void logger_close(void)
{
    pthread_mutex_lock(&g_mx);
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    pthread_mutex_unlock(&g_mx);
}
