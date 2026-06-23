/**
 * @file config.h
 * @brief Simple KEY=VALUE config file parser for C components.
 */

#ifndef PARKING_CONFIG_H
#define PARKING_CONFIG_H

int         config_load   (const char *path);
const char *config_get    (const char *key, const char *default_val);
int         config_get_int(const char *key, int default_val);

#endif /* PARKING_CONFIG_H */
