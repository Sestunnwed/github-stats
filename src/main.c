#include "config.h"
#include "cache.h"
#include "stats.h"
#include "svg_writer.h"
#include "log.h"

#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

#define STATS_CACHE_PATH "generated/stats_cache.bin"

int main(int argc, char **argv)
{
    GitStats stats;
    int use_cached = (argc >= 2 && strcmp(argv[1], "-g") == 0);
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [-g]\n", argv[0]);
        printf("  (no args): fetch from GitHub, generate SVG, and save cache\n");
        printf("  -g       : generate SVG from existing local cache only\n");
        return 0;
    }
    if (argc >= 2 && !use_cached) {
        log_error("unknown option: %s", argv[1]);
        printf("Usage: %s [-g]\n", argv[0]);
        return 1;
    }

    if (use_cached) {
        if (load_stats_cache(STATS_CACHE_PATH, &stats) != 0) {
            log_error("no valid cache found at %s, run ./github-stats first", STATS_CACHE_PATH);
            return 1;
        }
    } else {
        AppConfig cfg;
        if (load_config(&cfg) != 0) {
            return 1;
        }

        log_info("starting stats generation for user=%s (count_forks=%d)", cfg.user, cfg.count_forks);
        curl_global_init(CURL_GLOBAL_DEFAULT);

        if (build_stats(cfg.user, cfg.token, cfg.count_forks, &stats) != 0) {
            log_error("failed to collect GitHub stats for user=%s", cfg.user);
            curl_global_cleanup();
            return 1;
        }

        if (save_stats_cache(STATS_CACHE_PATH, &stats) != 0) {
            log_warn("failed to save cache: %s", STATS_CACHE_PATH);
        }

        curl_global_cleanup();
    }

    if (write_svgs(&stats) != 0) {
        log_error("failed to write SVG output");
        return 1;
    }

    log_info("completed stats generation: repos=%lld stars=%lld forks=%lld languages=%zu",
             stats.repos,
             stats.stars,
             stats.forks,
             stats.lang_count);

    printf("generated/overview.svg\n");
    printf("generated/languages.svg\n");
    return 0;
}
