#ifndef STATS_H
#define STATS_H

#include <stddef.h>

#define MAX_LANGS 256

typedef struct {
    char name[64];
    long long bytes;
    double percent;
    char color[16];
} LanguageStat;

typedef struct {
    char user[128];
    char display_name[128];
    long long stars;
    long long forks;
    long long repos;
    long long views;
    long long total_contributions;
    long long additions;
    long long deletions;
    LanguageStat langs[MAX_LANGS];
    size_t lang_count;
} GitStats;

int build_stats(const char *user, const char *token, int count_forks, GitStats *out);

#endif
