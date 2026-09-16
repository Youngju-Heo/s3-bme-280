#include "protocol.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "json-mini.h"

#define READ_CHUNK 32

static const protocol_ops_t *g_ops;

typedef struct { protocol_write_fn write; void *ctx; } writer_t;

static void emit(writer_t *w, const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof buf) n = sizeof buf - 1;
    w->write(w->ctx, buf, (size_t)n);
}

static void reply_error(writer_t *w, const char *code)
{
    emit(w, "{\"ok\":false,\"error\":\"%s\"}\n", code);
}

static void cmd_ping(writer_t *w)
{
    emit(w, "{\"ok\":true,\"firmware\":\"%s\",\"boot_id\":%u,\"uptime_s\":%lu,\"time_valid\":%s}\n",
         PROTOCOL_FIRMWARE_VERSION, g_ops->boot_id(g_ops->ctx), (unsigned long)g_ops->uptime_s(g_ops->ctx),
         g_ops->time_valid(g_ops->ctx) ? "true" : "false");
}

static void cmd_set_time(writer_t *w, const char *line)
{
    uint32_t epoch;
    if (!json_mini_get_uint(line, "epoch", &epoch)) { reply_error(w, "bad_request"); return; }
    g_ops->set_time(g_ops->ctx, epoch);
    emit(w, "{\"ok\":true,\"boot_id\":%u,\"uptime_s\":%lu}\n", g_ops->boot_id(g_ops->ctx), (unsigned long)g_ops->uptime_s(g_ops->ctx));
}

static void cmd_read_now(writer_t *w)
{
    bme280_reading_t r;
    if (g_ops->read_now(g_ops->ctx, &r) != 0) { reply_error(w, "sensor_error"); return; }
    int32_t t = r.temp_centi < 0 ? -r.temp_centi : r.temp_centi;
    emit(w, "{\"ok\":true,\"temp_c\":%s%ld.%02ld,\"hum_pct\":%lu.%02lu,\"pressure_pa\":%lu}\n",
         r.temp_centi < 0 ? "-" : "", (long)(t / 100), (long)(t % 100),
         (unsigned long)(r.hum_centi / 100), (unsigned long)(r.hum_centi % 100), (unsigned long)r.pressure_pa);
}

static void cmd_get_status(writer_t *w)
{
    emit(w, "{\"ok\":true,\"count\":%lu,\"capacity\":%lu,\"interval_s\":%lu,\"sensor_ok\":%s,\"time_valid\":%s,\"boot_id\":%u,\"uptime_s\":%lu}\n",
         (unsigned long)log_store_count(g_ops->store), (unsigned long)LOG_STORE_CAPACITY,
         (unsigned long)g_ops->interval_s(g_ops->ctx), g_ops->sensor_ok(g_ops->ctx) ? "true" : "false",
         g_ops->time_valid(g_ops->ctx) ? "true" : "false", g_ops->boot_id(g_ops->ctx),
         (unsigned long)g_ops->uptime_s(g_ops->ctx));
}

static void cmd_get_log(writer_t *w, const char *line)
{
    uint32_t offset = 0, limit = PROTOCOL_DEFAULT_LIMIT;
    json_mini_get_uint(line, "offset", &offset);
    json_mini_get_uint(line, "limit", &limit);
    if (limit > PROTOCOL_MAX_LIMIT) limit = PROTOCOL_MAX_LIMIT;

    uint32_t total = log_store_count(g_ops->store);
    emit(w, "{\"ok\":true,\"total\":%lu,\"offset\":%lu,\"records\":[", (unsigned long)total, (unsigned long)offset);

    bool first = true;
    for (uint32_t done = 0; done < limit && offset + done < total; done += READ_CHUNK) {
        log_record_t buf[READ_CHUNK];
        uint32_t want = limit - done < READ_CHUNK ? limit - done : READ_CHUNK;
        uint32_t n = 0;
        if (log_store_read(g_ops->store, offset + done, buf, want, &n) != 0) break;
        for (uint32_t i = 0; i < n; i++) {
            emit(w, "%s[%lu,%d,%u,%lu,%u,%u]", first ? "" : ",", (unsigned long)buf[i].timestamp, buf[i].temp_centi,
                 buf[i].hum_centi, (unsigned long)buf[i].pressure_pa, buf[i].flags, buf[i].boot_id);
            first = false;
        }
    }
    emit(w, "]}\n");
}

static void cmd_clear_log(writer_t *w)
{
    if (log_store_clear(g_ops->store) != 0) { reply_error(w, "store_error"); return; }
    emit(w, "{\"ok\":true}\n");
}

static void cmd_set_interval(writer_t *w, const char *line)
{
    uint32_t s;
    if (!json_mini_get_uint(line, "interval_s", &s)) { reply_error(w, "bad_request"); return; }
    if (g_ops->set_interval_s(g_ops->ctx, s) != 0) { reply_error(w, "out_of_range"); return; }
    emit(w, "{\"ok\":true}\n");
}

void protocol_init(const protocol_ops_t *ops) { g_ops = ops; }

void protocol_handle_line(const char *line, protocol_write_fn write, void *wctx)
{
    writer_t w = { write, wctx };
    char cmd[32];
    if (!json_mini_get_string(line, "cmd", cmd, sizeof cmd)) { reply_error(&w, "bad_request"); return; }

    if (strcmp(cmd, "ping") == 0) cmd_ping(&w);
    else if (strcmp(cmd, "set_time") == 0) cmd_set_time(&w, line);
    else if (strcmp(cmd, "read_now") == 0) cmd_read_now(&w);
    else if (strcmp(cmd, "get_status") == 0) cmd_get_status(&w);
    else if (strcmp(cmd, "get_log") == 0) cmd_get_log(&w, line);
    else if (strcmp(cmd, "clear_log") == 0) cmd_clear_log(&w);
    else if (strcmp(cmd, "set_interval") == 0) cmd_set_interval(&w, line);
    else reply_error(&w, "unknown_cmd");
}
