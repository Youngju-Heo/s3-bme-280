#include "json-mini.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_value(const char *json, const char *key)
{
    char pat[40];
    if (snprintf(pat, sizeof pat, "\"%s\"", key) >= (int)sizeof pat) return NULL;
    const char *search = json;
    for (;;) {
        const char *p = strstr(search, pat);
        if (!p) return NULL;
        const char *v = p + strlen(pat);
        while (*v == ' ') v++;
        if (*v == ':') {
            v++;
            while (*v == ' ') v++;
            return v;
        }
        search = p + 1;
    }
}

bool json_mini_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    const char *p = find_value(json, key);
    if (!p || *p != '"') return false;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return false;
    size_t n = (size_t)(end - p);
    if (n >= out_len) return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

bool json_mini_get_uint(const char *json, const char *key, uint32_t *out)
{
    const char *p = find_value(json, key);
    if (!p || !isdigit((unsigned char)*p)) return false;
    char *end;
    errno = 0;
    unsigned long v = strtoul(p, &end, 10);
    if (end == p) return false;
    if (errno == ERANGE || v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}
