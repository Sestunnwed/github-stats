#include "stats.h"

#include "github_api.h"

#include <cjson/cJSON.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

typedef struct {
    char **items;
    size_t count;
} PatternList;


static char *xstrdup(const char *s)
{
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n + 1);
    return d;
}

static void patterns_free(PatternList *p)
{
    if (!p) return;
    for (size_t i = 0; i < p->count; ++i) free(p->items[i]);
    free(p->items);
    p->items = NULL;
    p->count = 0;
}

static PatternList patterns_parse_env(const char *env_name)
{
    PatternList out = {0};
    const char *raw = getenv(env_name);
    if (!raw || !*raw) return out;

    char *copy = xstrdup(raw);
    if (!copy) return out;

    for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        size_t n = strlen(tok);
        while (n > 0 && (tok[n - 1] == ' ' || tok[n - 1] == '\t')) tok[--n] = '\0';
        if (n == 0) continue;

        char **next = realloc(out.items, sizeof(char *) * (out.count + 1));
        if (!next) continue;
        out.items = next;
        out.items[out.count] = xstrdup(tok);
        if (!out.items[out.count]) continue;
        out.count++;
    }

    free(copy);
    return out;
}

static int patterns_match(const PatternList *patterns, const char *text)
{
    if (!patterns || !text) return 0;
    for (size_t i = 0; i < patterns->count; ++i) {
        if (strcmp(patterns->items[i], text) == 0) return 1;
        if (fnmatch(patterns->items[i], text, 0) == 0) return 1;
    }
    return 0;
}

static LanguageStat *find_or_add_lang(GitStats *s, const char *name)
{
    for (size_t i = 0; i < s->lang_count; ++i) {
        if (strcmp(s->langs[i].name, name) == 0) return &s->langs[i];
    }
    if (s->lang_count >= MAX_LANGS) return NULL;
    LanguageStat *n = &s->langs[s->lang_count++];
    memset(n, 0, sizeof(*n));
    snprintf(n->name, sizeof(n->name), "%s", name);
    snprintf(n->color, sizeof(n->color), "%s", "#000000");
    return n;
}

static int set_has(char **set, size_t count, const char *name)
{
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(set[i], name) == 0) return 1;
    }
    return 0;
}

static int set_add(char ***set, size_t *count, const char *name)
{
    if (set_has(*set, *count, name)) return 0;
    char **next = realloc(*set, sizeof(char *) * (*count + 1));
    if (!next) return -1;
    *set = next;
    (*set)[*count] = xstrdup(name);
    if (!(*set)[*count]) return -1;
    (*count)++;
    return 0;
}

static void free_set(char **set, size_t count)
{
    for (size_t i = 0; i < count; ++i) free(set[i]);
    free(set);
}

static void ingest_languages(const char *url, const char *token, const PatternList *exclude_langs, GitStats *s)
{
    char *json = github_get(url, token);
    if (!json) {
        log_warn("failed to fetch language breakdown: %s", url);
        return;
    }

    size_t payload_size = strlen(json);
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root || !cJSON_IsObject(root)) {
        log_warn("invalid language response payload from %s", url);
        cJSON_Delete(root);
        return;
    }

    long long repo_lang_total = 0;
    cJSON *kv = NULL;
    cJSON_ArrayForEach(kv, root)
    {
        if (!cJSON_IsNumber(kv) || !kv->string) continue;
        if (patterns_match(exclude_langs, kv->string)) continue;
        LanguageStat *ls = find_or_add_lang(s, kv->string);
        if (ls) ls->bytes += (long long)kv->valuedouble;
        repo_lang_total += (long long)kv->valuedouble;
    }

    cJSON_Delete(root);
    log_info("ingested language payload: bytes=%zu language_total=%lld source=%s", payload_size, repo_lang_total, url);
}



static void hydrate_language_colors_from_graphql(const char *token, int count_forks, const PatternList *exclude_repos, GitStats *out)
{
    const char *q = "query { viewer { repositories(first: 100, orderBy: {field: UPDATED_AT, direction: DESC}, after: %s) { pageInfo { hasNextPage endCursor } nodes { nameWithOwner isFork languages(first: 10, orderBy: {field: SIZE, direction: DESC}) { edges { node { name color } } } } } } }";
    char cursor[512] = {0};
    int has_next = 1;

    while (has_next) {
        char query[4096];
        if (cursor[0]) {
            snprintf(query, sizeof(query), q, cursor);
        } else {
            snprintf(query, sizeof(query), q, "null");
        }

        char *json = github_graphql(token, query);
        if (!json) return;
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (!root) return;

        cJSON *repos = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(
                cJSON_GetObjectItemCaseSensitive(
                    cJSON_GetObjectItemCaseSensitive(root, "data"), "viewer"),
                "repositories"),
            "nodes");

        if (cJSON_IsArray(repos)) {
            cJSON *repo = NULL;
            cJSON_ArrayForEach(repo, repos)
            {
                cJSON *full_name = cJSON_GetObjectItemCaseSensitive(repo, "nameWithOwner");
                cJSON *fork = cJSON_GetObjectItemCaseSensitive(repo, "isFork");
                if (!cJSON_IsString(full_name) || !full_name->valuestring) continue;
                if (patterns_match(exclude_repos, full_name->valuestring)) continue;
                if (!count_forks && cJSON_IsBool(fork) && cJSON_IsTrue(fork)) continue;

                cJSON *edges = cJSON_GetObjectItemCaseSensitive(
                    cJSON_GetObjectItemCaseSensitive(repo, "languages"),
                    "edges");
                if (!cJSON_IsArray(edges)) continue;
                cJSON *edge = NULL;
                cJSON_ArrayForEach(edge, edges)
                {
                    cJSON *node = cJSON_GetObjectItemCaseSensitive(edge, "node");
                    cJSON *name = cJSON_GetObjectItemCaseSensitive(node, "name");
                    cJSON *color = cJSON_GetObjectItemCaseSensitive(node, "color");
                    if (!cJSON_IsString(name) || !name->valuestring) continue;
                    for (size_t i = 0; i < out->lang_count; ++i) {
                        if (strcmp(out->langs[i].name, name->valuestring) == 0) {
                            if (cJSON_IsString(color) && color->valuestring && *color->valuestring) {
                                snprintf(out->langs[i].color, sizeof(out->langs[i].color), "%s", color->valuestring);
                            }
                            break;
                        }
                    }
                }
            }
        }

        cJSON *page_info = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(
                cJSON_GetObjectItemCaseSensitive(
                    cJSON_GetObjectItemCaseSensitive(root, "data"), "viewer"),
                "repositories"),
            "pageInfo");
        cJSON *hn = cJSON_GetObjectItemCaseSensitive(page_info, "hasNextPage");
        cJSON *ec = cJSON_GetObjectItemCaseSensitive(page_info, "endCursor");
        has_next = cJSON_IsBool(hn) && cJSON_IsTrue(hn);
        if (has_next && cJSON_IsString(ec) && ec->valuestring) {
            snprintf(cursor, sizeof(cursor), "\"%s\"", ec->valuestring);
        } else {
            has_next = 0;
        }

        cJSON_Delete(root);
    }

    log_info("hydrated language colors from GraphQL");
}
static long long fetch_total_contributions(const char *token)
{
    char *years_json = github_graphql(token, "query { viewer { contributionsCollection { contributionYears } } }");
    if (!years_json) return 0;

    cJSON *root = cJSON_Parse(years_json);
    free(years_json);
    if (!root) return 0;

    cJSON *years = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(
                cJSON_GetObjectItemCaseSensitive(root, "data"), "viewer"),
            "contributionsCollection"),
        "contributionYears");

    long long total = 0;
    if (cJSON_IsArray(years)) {
        cJSON *y = NULL;
        cJSON_ArrayForEach(y, years)
        {
            if (!cJSON_IsNumber(y)) continue;
            int year = y->valueint;
            char q[512];
            snprintf(q,
                     sizeof(q),
                     "query { viewer { contributionsCollection(from: \\\"%d-01-01T00:00:00Z\\\", to: \\\"%d-01-01T00:00:00Z\\\") { contributionCalendar { totalContributions } } } }",
                     year,
                     year + 1);
            char *year_json = github_graphql(token, q);
            if (!year_json) continue;
            cJSON *yr = cJSON_Parse(year_json);
            free(year_json);
            if (!yr) continue;
            cJSON *tc = cJSON_GetObjectItemCaseSensitive(
                cJSON_GetObjectItemCaseSensitive(
                    cJSON_GetObjectItemCaseSensitive(
                        cJSON_GetObjectItemCaseSensitive(
                            cJSON_GetObjectItemCaseSensitive(yr, "data"), "viewer"),
                        "contributionsCollection"),
                    "contributionCalendar"),
                "totalContributions");
            if (cJSON_IsNumber(tc)) total += (long long)tc->valuedouble;
            cJSON_Delete(yr);
        }
    }

    cJSON_Delete(root);
    log_info("calculated total contributions=%lld", total);
    return total;
}

static void fetch_lines_changed_and_views(const char *user, const char *token, char **all_repos, size_t all_repo_count, GitStats *out)
{
    long long additions = 0;
    long long deletions = 0;
    long long views = 0;

    for (size_t i = 0; i < all_repo_count; ++i) {
        const char *repo = all_repos[i];
        log_info("lines_changed scanning repo=%s (%zu/%zu)", repo, i + 1, all_repo_count);

        for (int page = 1; page <= 100; ++page) {
            char commits_url[1024];
            snprintf(commits_url,
                     sizeof(commits_url),
                     "https://api.github.com/repos/%s/commits?author=%s&per_page=100&page=%d",
                     repo,
                     user,
                     page);
            char *commits_json = github_get(commits_url, token);
            if (!commits_json) break;

            cJSON *arr = cJSON_Parse(commits_json);
            free(commits_json);
            if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
                cJSON_Delete(arr);
                break;
            }

            int n = cJSON_GetArraySize(arr);
            for (int j = 0; j < n; ++j) {
                cJSON *commit = cJSON_GetArrayItem(arr, j);
                cJSON *sha = cJSON_GetObjectItemCaseSensitive(commit, "sha");
                if (!cJSON_IsString(sha) || !sha->valuestring) continue;

                char detail_url[1200];
                snprintf(detail_url,
                         sizeof(detail_url),
                         "https://api.github.com/repos/%s/commits/%s",
                         repo,
                         sha->valuestring);
                char *detail_json = github_get(detail_url, token);
                if (!detail_json) continue;
                cJSON *detail = cJSON_Parse(detail_json);
                free(detail_json);
                if (!detail) continue;

                cJSON *stats = cJSON_GetObjectItemCaseSensitive(detail, "stats");
                cJSON *a = cJSON_GetObjectItemCaseSensitive(stats, "additions");
                cJSON *d = cJSON_GetObjectItemCaseSensitive(stats, "deletions");
                if (cJSON_IsNumber(a)) additions += (long long)a->valuedouble;
                if (cJSON_IsNumber(d)) deletions += (long long)d->valuedouble;
                cJSON_Delete(detail);
            }
            cJSON_Delete(arr);

            if (n < 100) break;
        }

        char view_url[1024];
        snprintf(view_url, sizeof(view_url), "https://api.github.com/repos/%s/traffic/views", repo);
        char *views_json = github_get(view_url, token);
        if (views_json) {
            cJSON *vr = cJSON_Parse(views_json);
            free(views_json);
            if (vr) {
                cJSON *va = cJSON_GetObjectItemCaseSensitive(vr, "views");
                if (cJSON_IsArray(va)) {
                    cJSON *it = NULL;
                    cJSON_ArrayForEach(it, va)
                    {
                        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(it, "count");
                        if (cJSON_IsNumber(cnt)) views += (long long)cnt->valuedouble;
                    }
                }
                cJSON_Delete(vr);
            }
        }
    }

    out->additions = additions;
    out->deletions = deletions;
    out->views = views;
    log_info("lines_changed finished additions=%lld deletions=%lld views=%lld", additions, deletions, views);
}

static int cmp_lang(const void *a, const void *b)
{
    const LanguageStat *la = (const LanguageStat *)a;
    const LanguageStat *lb = (const LanguageStat *)b;
    if (la->bytes < lb->bytes) return 1;
    if (la->bytes > lb->bytes) return -1;
    return 0;
}

int build_stats(const char *user, const char *token, int count_forks, GitStats *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->user, sizeof(out->user), "%s", user);
    snprintf(out->display_name, sizeof(out->display_name), "%s", user);

    PatternList exclude_repos = patterns_parse_env("EXCLUDED");
    PatternList exclude_langs = patterns_parse_env("EXCLUDED_LANGS");
    log_info("exclude patterns loaded: repos=%zu langs=%zu", exclude_repos.count, exclude_langs.count);

    char profile_url[512];
    snprintf(profile_url, sizeof(profile_url), "https://api.github.com/users/%s", user);
    char *profile = github_get(profile_url, token);
    if (profile) {
        cJSON *p = cJSON_Parse(profile);
        free(profile);
        if (p) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(p, "name");
            if (cJSON_IsString(name) && name->valuestring && *name->valuestring) {
                snprintf(out->display_name, sizeof(out->display_name), "%s", name->valuestring);
            }
            cJSON_Delete(p);
        }
    }

    char **owned_repos = NULL;
    size_t owned_count = 0;
    char **all_repos = NULL;
    size_t all_count = 0;

    for (int page = 1; page <= 100; ++page) {
        char url[1024];
        snprintf(url,
                 sizeof(url),
                 "https://api.github.com/user/repos?per_page=100&page=%d&sort=updated&direction=desc",
                 page);
        char *json = github_get(url, token);
        if (!json) break;

        cJSON *arr = cJSON_Parse(json);
        free(json);
        if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
            cJSON_Delete(arr);
            break;
        }

        int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n; ++i) {
            cJSON *repo = cJSON_GetArrayItem(arr, i);
            cJSON *full_name = cJSON_GetObjectItemCaseSensitive(repo, "full_name");
            cJSON *fork = cJSON_GetObjectItemCaseSensitive(repo, "fork");
            if (!cJSON_IsString(full_name) || !full_name->valuestring) continue;
            const char *repo_name = full_name->valuestring;
            if (patterns_match(&exclude_repos, repo_name)) continue;

            if (set_add(&all_repos, &all_count, repo_name) != 0) continue;

            if (!count_forks && cJSON_IsBool(fork) && cJSON_IsTrue(fork)) {
                continue;
            }

            if (set_add(&owned_repos, &owned_count, repo_name) != 0) continue;

            cJSON *stars = cJSON_GetObjectItemCaseSensitive(repo, "stargazers_count");
            cJSON *forks = cJSON_GetObjectItemCaseSensitive(repo, "forks_count");
            cJSON *lang_url = cJSON_GetObjectItemCaseSensitive(repo, "languages_url");
            if (cJSON_IsNumber(stars)) out->stars += (long long)stars->valuedouble;
            if (cJSON_IsNumber(forks)) out->forks += (long long)forks->valuedouble;
            if (cJSON_IsString(lang_url) && lang_url->valuestring) {
                ingest_languages(lang_url->valuestring, token, &exclude_langs, out);
            }
        }
        cJSON_Delete(arr);
        if (n < 100) break;
    }

    out->repos = (long long)all_count;
    hydrate_language_colors_from_graphql(token, count_forks, &exclude_repos, out);
    out->total_contributions = fetch_total_contributions(token);

    // Merge Jupyter Notebook into Python to match python behavior.
    size_t py_i = (size_t)-1, nb_i = (size_t)-1;
    for (size_t i = 0; i < out->lang_count; ++i) {
        if (strcmp(out->langs[i].name, "Python") == 0) py_i = i;
        if (strcmp(out->langs[i].name, "Jupyter Notebook") == 0) nb_i = i;
    }
    if (py_i != (size_t)-1 && nb_i != (size_t)-1) {
        out->langs[py_i].bytes += out->langs[nb_i].bytes;
        if (strcmp(out->langs[py_i].color, "#000000") == 0 && strcmp(out->langs[nb_i].color, "#000000") != 0) {
            char color_copy[16];
            snprintf(color_copy, sizeof(color_copy), "%s", out->langs[nb_i].color);
            snprintf(out->langs[py_i].color, sizeof(out->langs[py_i].color), "%s", color_copy);
        }
        for (size_t i = nb_i; i + 1 < out->lang_count; ++i) out->langs[i] = out->langs[i + 1];
        out->lang_count--;
    }

    long long total = 0;
    for (size_t i = 0; i < out->lang_count; ++i) total += out->langs[i].bytes;
    if (total > 0) {
        for (size_t i = 0; i < out->lang_count; ++i) {
            out->langs[i].percent = (100.0 * out->langs[i].bytes) / (double)total;
        }
    }

    qsort(out->langs, out->lang_count, sizeof(LanguageStat), cmp_lang);

    fetch_lines_changed_and_views(user, token, all_repos, all_count, out);

    free_set(owned_repos, owned_count);
    free_set(all_repos, all_count);
    patterns_free(&exclude_repos);
    patterns_free(&exclude_langs);

    log_info("stats built: repos=%lld stars=%lld forks=%lld contributions=%lld lang_count=%zu", out->repos, out->stars, out->forks, out->total_contributions, out->lang_count);
    return 0;
}
