/**
 * @file config.c
 * @brief KEY=VALUE config file parser for C components.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_ENTRIES 128
#define MAX_LINE    256

typedef struct { char key[MAX_LINE]; char val[MAX_LINE]; } entry_t;

static entry_t g_entries[MAX_ENTRIES];
static int     g_count = 0;

static void trim(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

int config_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[MAX_LINE];
    g_count = 0;

    while (fgets(line, sizeof(line), fp) && g_count < MAX_ENTRIES) {
        line[strcspn(line, "\r\n")] = '\0';
        trim(line);
        if (!line[0] || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = line, *v = eq + 1;
        trim(k); trim(v);
        snprintf(g_entries[g_count].key, MAX_LINE, "%s", k);
        snprintf(g_entries[g_count].val, MAX_LINE, "%s", v);
        g_count++;
    }
    fclose(fp);
    return 0;
}

const char *config_get(const char *key, const char *default_val)
{
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_entries[i].key, key) == 0)
            return g_entries[i].val;
    return default_val;
}

int config_get_int(const char *key, int default_val)
{
    const char *v = config_get(key, NULL);
    return v ? atoi(v) : default_val;
}
