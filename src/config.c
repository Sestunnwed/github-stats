#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

static int read_env(char *dst, size_t n, const char *k1, const char *k2)
{
    const char *v = getenv(k1);
    if ((!v || !*v) && k2) {
        v = getenv(k2);
    }
    if (!v || !*v) {
        return 0;
    }
    snprintf(dst, n, "%s", v);
    return 1;
}

int load_config(AppConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    if (!read_env(cfg->token, sizeof(cfg->token), "ACCESS_TOKEN", "GITHUB_TOKEN")) {
        log_error("missing ACCESS_TOKEN or GITHUB_TOKEN");
        return -1;
    }

    if (!read_env(cfg->user, sizeof(cfg->user), "GITHUB_ACTOR", "GITHUB_USER")) {
        log_error("missing GITHUB_ACTOR or GITHUB_USER");
        return -1;
    }

    const char *count = getenv("COUNT_STATS_FROM_FORKS");
    cfg->count_forks = count && *count;
    log_info("configuration loaded for user=%s", cfg->user);
    return 0;
}
