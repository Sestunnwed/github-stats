#include "github_api.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

typedef struct {
    char *buf;
    size_t len;
} Buffer;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t add = size * nmemb;
    Buffer *b = (Buffer *)userdata;
    char *next = realloc(b->buf, b->len + add + 1);
    if (!next) {
        return 0;
    }
    b->buf = next;
    memcpy(b->buf + b->len, ptr, add);
    b->len += add;
    b->buf[b->len] = '\0';
    return add;
}

char *github_get_with_accept(const char *url, const char *token, const char *accept)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("curl_easy_init failed");
        return NULL;
    }

    Buffer b = {.buf = malloc(1), .len = 0};
    if (!b.buf) {
        log_error("failed to allocate response buffer");
        curl_easy_cleanup(curl);
        return NULL;
    }
    b.buf[0] = '\0';

    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);

    struct curl_slist *h = NULL;
    h = curl_slist_append(h, auth);
    h = curl_slist_append(h, "X-GitHub-Api-Version: 2022-11-28");
    h = curl_slist_append(h, accept ? accept : "Accept: application/vnd.github+json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "git-stat-c");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_slist_free_all(h);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || code >= 400) {
        log_warn("github request failed: code=%ld curl=%d url=%s", code, (int)rc, url);
        free(b.buf);
        return NULL;
    }

    log_info("github request success: code=%ld bytes=%zu url=%s", code, b.len, url);
    return b.buf;
}

char *github_get(const char *url, const char *token)
{
    return github_get_with_accept(url, token, "Accept: application/vnd.github+json");
}

char *github_graphql(const char *token, const char *query)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("curl_easy_init failed for graphql");
        return NULL;
    }

    Buffer b = {.buf = malloc(1), .len = 0};
    if (!b.buf) {
        curl_easy_cleanup(curl);
        return NULL;
    }
    b.buf[0] = '\0';

    char auth[512];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);

    size_t qlen = strlen(query);
    char *body = malloc(qlen * 2 + 64);
    if (!body) {
        free(b.buf);
        curl_easy_cleanup(curl);
        return NULL;
    }
    char *w = body;
    strcpy(w, "{\"query\":\"");
    w += strlen(w);
    for (const char *p = query; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            *w++ = '\\';
            *w++ = *p;
        } else if (*p == '\n' || *p == '\r') {
            *w++ = ' ';
        } else {
            *w++ = *p;
        }
    }
    strcpy(w, "\"}");

    struct curl_slist *h = NULL;
    h = curl_slist_append(h, auth);
    h = curl_slist_append(h, "X-GitHub-Api-Version: 2022-11-28");
    h = curl_slist_append(h, "Accept: application/vnd.github+json");
    h = curl_slist_append(h, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/graphql");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "git-stat-c");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    free(body);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || code >= 400) {
        log_warn("github graphql failed: code=%ld curl=%d", code, (int)rc);
        free(b.buf);
        return NULL;
    }

    log_info("github graphql success: code=%ld bytes=%zu", code, b.len);
    return b.buf;
}

int github_paginated_repos(const char *user, const char *token, char ***pages, size_t *count)
{
    *pages = NULL;
    *count = 0;

    log_info("starting paginated repo fetch for user=%s", user);

    for (int page = 1; page <= 100; ++page) {
        char url[512];
        snprintf(url, sizeof(url), "https://api.github.com/users/%s/repos?per_page=100&page=%d", user, page);
        char *json = github_get(url, token);
        if (!json) {
            log_warn("failed to fetch repos page=%d for user=%s", page, user);
            return (*count > 0) ? 0 : -1;
        }

        char **next = realloc(*pages, sizeof(char *) * (*count + 1));
        if (!next) {
            log_error("failed to grow repo pages buffer");
            free(json);
            return -1;
        }
        *pages = next;
        log_info("fetched repos page=%d for user=%s", page, user);
        (*pages)[*count] = json;
        (*count)++;

        if (strstr(json, "\"id\"") == NULL) {
            break;
        }
        if (strstr(json, "\"full_name\"") == NULL) {
            break;
        }
        if (strstr(json, "\"language\"") == NULL) {
            break;
        }
        if (strstr(json, "],") == NULL && strstr(json, "}") != NULL) {
            break;
        }

        if (strlen(json) < 64) {
            break;
        }
    }

    log_info("completed paginated repo fetch for user=%s pages=%zu", user, *count);
    return 0;
}
