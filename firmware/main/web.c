#include "web.h"
#include <stdbool.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"

#include "protocol.h"
#include "web-bridge.h"

static const char *TAG = "web";
static web_lock_t g_lock;

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");   // always pick up a newly flashed page
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);   // EMBED_TXTFILES appends a NUL
}

// httpd runs handlers sequentially in one task, so a single static response buffer is safe.
#define RESPONSE_MAX 24576
static char g_resp[RESPONSE_MAX];
static size_t g_resp_len;
static bool g_resp_overflow;

static void buffer_write(void *ctx, const char *data, size_t len)
{
    (void)ctx;
    if (g_resp_overflow) return;
    if (len > RESPONSE_MAX - g_resp_len) { g_resp_overflow = true; return; }
    memcpy(g_resp + g_resp_len, data, len);
    g_resp_len += len;
}

static esp_err_t api_handler(httpd_req_t *req)
{
    char query[128] = "";
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen >= sizeof query) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "query too long");
    if (qlen > 0) httpd_req_get_url_query_str(req, query, sizeof query);

    const char *method = req->method == HTTP_POST ? "POST" : "GET";
    char line[PROTOCOL_MAX_LINE];
    httpd_resp_set_type(req, "application/json");
    if (!web_bridge_build_request(method, query, line, sizeof line)) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"forbidden\"}\n", HTTPD_RESP_USE_STRLEN);
    }
    g_resp_len = 0;
    g_resp_overflow = false;
    g_lock.lock(g_lock.ctx);
    protocol_handle_line(line, buffer_write, NULL);
    g_lock.unlock(g_lock.ctx);
    if (g_resp_overflow) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"response_too_large\"}\n", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, g_resp, (ssize_t)g_resp_len);
}

esp_err_t web_start(const web_lock_t *lock)
{
    g_lock = *lock;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;   // reclaim the least-recently-used socket when all 7 are held by vanished clients
    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) return err;

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
    httpd_uri_t api_get = { .uri = "/api", .method = HTTP_GET, .handler = api_handler };
    httpd_uri_t api_post = { .uri = "/api", .method = HTTP_POST, .handler = api_handler };
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &api_get);
    httpd_register_uri_handler(server, &api_post);
    ESP_LOGI(TAG, "http server started");
    return ESP_OK;
}
