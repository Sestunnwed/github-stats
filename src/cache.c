#include "cache.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

typedef struct {
    char magic[8];
    uint32_t version;
} CacheHeader;

static const char *CACHE_MAGIC = "GSTATS1";
static const uint32_t CACHE_VERSION = 1;

int save_stats_cache(const char *path, const GitStats *stats)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        log_error("failed to open cache file for write: %s", path);
        return -1;
    }

    CacheHeader h = {0};
    snprintf(h.magic, sizeof(h.magic), "%s", CACHE_MAGIC);
    h.version = CACHE_VERSION;

    if (fwrite(&h, sizeof(h), 1, fp) != 1 || fwrite(stats, sizeof(*stats), 1, fp) != 1) {
        log_error("failed to write cache file: %s", path);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    log_info("saved stats cache: %s", path);
    return 0;
}

int load_stats_cache(const char *path, GitStats *stats)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        log_error("failed to open cache file for read: %s", path);
        return -1;
    }

    CacheHeader h = {0};
    if (fread(&h, sizeof(h), 1, fp) != 1) {
        log_error("failed to read cache header: %s", path);
        fclose(fp);
        return -1;
    }

    if (strncmp(h.magic, CACHE_MAGIC, sizeof(h.magic)) != 0 || h.version != CACHE_VERSION) {
        log_error("cache file format mismatch: %s", path);
        fclose(fp);
        return -1;
    }

    if (fread(stats, sizeof(*stats), 1, fp) != 1) {
        log_error("failed to read cached stats payload: %s", path);
        fclose(fp);
        return -1;
    }

    fclose(fp);
    log_info("loaded stats cache: %s", path);
    return 0;
}
