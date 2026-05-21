#ifndef CACHE_H
#define CACHE_H

#include "stats.h"

int save_stats_cache(const char *path, const GitStats *stats);
int load_stats_cache(const char *path, GitStats *stats);

#endif
