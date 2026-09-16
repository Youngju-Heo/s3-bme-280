#include "log-store.h"

#define SLOTS LOG_STORE_TOTAL_SLOTS
#define SPS LOG_STORE_SLOTS_PER_SECTOR

static int read_slot(log_store_t *s, uint32_t slot, uint8_t buf[LOG_RECORD_SIZE])
{
    return s->flash.read(s->flash.ctx, slot * LOG_RECORD_SIZE, buf, LOG_RECORD_SIZE);
}

static int sector_is_erased(log_store_t *s, uint32_t sector, bool *erased)
{
    uint8_t buf[LOG_RECORD_SIZE];
    int rc = read_slot(s, sector * SPS, buf);
    if (rc) return rc;
    *erased = log_record_is_empty(buf);
    return 0;
}

// Erases a sector; if the oldest record lived there, the tail moves past it (rolling).
static int erase_sector(log_store_t *s, uint32_t sector)
{
    int rc = s->flash.erase_sector(s->flash.ctx, sector);
    if (rc) return rc;
    uint32_t first = sector * SPS;
    uint32_t last = first + SPS;
    if (log_store_count(s) > 0 && s->tail >= first && s->tail < last) {
        s->tail = last % SLOTS;
    }
    return 0;
}

int log_store_init(log_store_t *s, const log_store_flash_t *flash)
{
    s->flash = *flash;
    s->head = 0;
    s->tail = 0;

    bool erased[LOG_STORE_SECTORS];
    uint32_t n_erased = 0;
    for (uint32_t i = 0; i < LOG_STORE_SECTORS; i++) {
        int rc = sector_is_erased(s, i, &erased[i]);
        if (rc) return rc;
        if (erased[i]) n_erased++;
    }
    if (n_erased == LOG_STORE_SECTORS) return 0;
    if (n_erased == 0) return LOG_STORE_ERR_CORRUPT;

    // Erased sectors form one circular run; data sits between its end and its start.
    uint32_t run_start = 0;
    for (uint32_t i = 0; i < LOG_STORE_SECTORS; i++) {
        uint32_t prev = (i + LOG_STORE_SECTORS - 1) % LOG_STORE_SECTORS;
        if (erased[i] && !erased[prev]) { run_start = i; break; }
    }
    uint32_t run_end = run_start;
    while (erased[(run_end + 1) % LOG_STORE_SECTORS]) run_end = (run_end + 1) % LOG_STORE_SECTORS;

    s->tail = ((run_end + 1) % LOG_STORE_SECTORS) * SPS;

    uint32_t head_sector = (run_start + LOG_STORE_SECTORS - 1) % LOG_STORE_SECTORS;
    s->head = (run_start * SPS) % SLOTS;   // assume the head sector is full unless an empty slot is found
    for (uint32_t slot = head_sector * SPS; slot < head_sector * SPS + SPS; slot++) {
        uint8_t buf[LOG_RECORD_SIZE];
        int rc = read_slot(s, slot, buf);
        if (rc) return rc;
        if (log_record_is_empty(buf)) { s->head = slot; break; }
    }
    return 0;
}

int log_store_append(log_store_t *s, const log_record_t *rec)
{
    if (s->head % SPS == 0) {
        uint32_t sector = s->head / SPS;
        bool erased;
        int rc = sector_is_erased(s, sector, &erased);
        if (rc) return rc;
        if (!erased) {                     // invariant broken (e.g. power loss); reclaim the sector
            rc = erase_sector(s, sector);
            if (rc) return rc;
        }
        rc = erase_sector(s, (sector + 1) % LOG_STORE_SECTORS);   // erase-ahead keeps >= 1 sector free
        if (rc) return rc;
    }
    uint8_t buf[LOG_RECORD_SIZE];
    log_record_encode(rec, buf);
    int rc = s->flash.write(s->flash.ctx, s->head * LOG_RECORD_SIZE, buf, LOG_RECORD_SIZE);
    if (rc) return rc;
    s->head = (s->head + 1) % SLOTS;
    return 0;
}

uint32_t log_store_count(const log_store_t *s)
{
    return (s->head + SLOTS - s->tail) % SLOTS;
}

int log_store_read(log_store_t *s, uint32_t offset, log_record_t *out, uint32_t max, uint32_t *n_read)
{
    uint32_t count = log_store_count(s);
    *n_read = 0;
    for (uint32_t i = 0; i < max && offset + i < count; i++) {
        uint8_t buf[LOG_RECORD_SIZE];
        int rc = read_slot(s, (s->tail + offset + i) % SLOTS, buf);
        if (rc) return rc;
        if (log_record_decode(buf, &out[*n_read])) (*n_read)++;
    }
    return 0;
}

int log_store_clear(log_store_t *s)
{
    for (uint32_t i = 0; i < LOG_STORE_SECTORS; i++) {
        int rc = s->flash.erase_sector(s->flash.ctx, i);
        if (rc) return rc;
    }
    s->head = 0;
    s->tail = 0;
    return 0;
}
