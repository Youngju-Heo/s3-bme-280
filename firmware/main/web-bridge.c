#include "web-bridge.h"
#include <stdio.h>
#include <string.h>

#define MAX_VALUE 24

static bool is_cmd_char(char c) { return (c >= 'a' && c <= 'z') || c == '_'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool all(const char *s, bool (*ok)(char))
{
    if (*s == '\0') return false;
    for (; *s; s++) if (!ok(*s)) return false;
    return true;
}

static bool allowed(const char *method, const char *cmd)
{
    if (strcmp(method, "GET") == 0) {
        return strcmp(cmd, "ping") == 0 || strcmp(cmd, "read_now") == 0 ||
               strcmp(cmd, "get_status") == 0 || strcmp(cmd, "get_log") == 0;
    }
    if (strcmp(method, "POST") == 0) return strcmp(cmd, "set_time") == 0;
    return false;
}

bool web_bridge_build_request(const char *method, const char *query, char *out, size_t out_len)
{
    char cmd[MAX_VALUE] = "", offset[MAX_VALUE] = "", limit[MAX_VALUE] = "", epoch[MAX_VALUE] = "";
    const char *p = query;
    while (*p) {
        const char *eq = strchr(p, '=');
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        if (!eq || eq > end) return false;
        size_t klen = (size_t)(eq - p), vlen = (size_t)(end - eq - 1);
        if (vlen == 0 || vlen >= MAX_VALUE) return false;
        char *dst;
        if (klen == 3 && strncmp(p, "cmd", 3) == 0) dst = cmd;
        else if (klen == 6 && strncmp(p, "offset", 6) == 0) dst = offset;
        else if (klen == 5 && strncmp(p, "limit", 5) == 0) dst = limit;
        else if (klen == 5 && strncmp(p, "epoch", 5) == 0) dst = epoch;
        else return false;
        memcpy(dst, eq + 1, vlen);
        dst[vlen] = '\0';
        p = amp ? amp + 1 : end;
    }
    if (!all(cmd, is_cmd_char) || !allowed(method, cmd)) return false;
    if (*offset && !all(offset, is_digit)) return false;
    if (*limit && !all(limit, is_digit)) return false;
    if (*epoch && !all(epoch, is_digit)) return false;

    int n = snprintf(out, out_len, "{\"cmd\":\"%s\"", cmd);
    if (n < 0 || (size_t)n >= out_len) return false;
    size_t used = (size_t)n;
    const char *keys[] = { "offset", "limit", "epoch" };
    const char *vals[] = { offset, limit, epoch };
    for (int i = 0; i < 3; i++) {
        if (!*vals[i]) continue;
        n = snprintf(out + used, out_len - used, ",\"%s\":%s", keys[i], vals[i]);
        if (n < 0 || (size_t)n >= out_len - used) return false;
        used += (size_t)n;
    }
    n = snprintf(out + used, out_len - used, "}");
    return n >= 0 && (size_t)n < out_len - used;
}
