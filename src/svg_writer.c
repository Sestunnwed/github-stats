#include "svg_writer.h"

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "log.h"

#define TEMPLATE_OVERVIEW "templates/overview.svg"
#define TEMPLATE_LANGUAGES "templates/languages.svg"
#define DEFAULT_LANGUAGE_COLOR "#8B949E"

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static int sb_reserve(StrBuf *b, size_t want)
{
    if (want <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < want) cap *= 2;
    char *n = realloc(b->data, cap);
    if (!n) return -1;
    b->data = n;
    b->cap = cap;
    return 0;
}

static int sb_appendf(StrBuf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return -1;
    }
    if (sb_reserve(b, b->len + (size_t)n + 1) != 0) {
        va_end(ap2);
        return -1;
    }
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
    return 0;
}

static void sb_free(StrBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        log_error("failed to open template: %s", path);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        log_error("failed to allocate template buffer for %s", path);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';

    log_info("loaded template: %s (%zu bytes)", path, n);
    return buf;
}

static char *replace_all(const char *src, const char *key, const char *value)
{
    if (!src || !key || !value) return NULL;

    size_t src_len = strlen(src);
    size_t key_len = strlen(key);
    size_t val_len = strlen(value);

    if (key_len == 0) {
        char *dup = malloc(src_len + 1);
        if (!dup) return NULL;
        memcpy(dup, src, src_len + 1);
        return dup;
    }

    size_t count = 0;
    const char *p = src;
    while ((p = strstr(p, key)) != NULL) {
        ++count;
        p += key_len;
    }

    size_t out_len = src_len + count * (val_len - key_len);
    char *out = malloc(out_len + 1);
    if (!out) return NULL;

    const char *cur = src;
    char *dst = out;
    while ((p = strstr(cur, key)) != NULL) {
        size_t pre = (size_t)(p - cur);
        memcpy(dst, cur, pre);
        dst += pre;
        memcpy(dst, value, val_len);
        dst += val_len;
        cur = p + key_len;
    }
    strcpy(dst, cur);

    return out;
}

static int write_text_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (!fp) {
        log_error("failed to open %s for writing", path);
        return -1;
    }
    if (fputs(content, fp) == EOF) {
        fclose(fp);
        log_error("failed to write content to %s", path);
        return -1;
    }
    fclose(fp);
    log_info("wrote %s", path);
    return 0;
}

static void ensure_generated_dir(void)
{
    if (mkdir("generated", 0755) == 0) {
        log_info("created output directory: generated");
        return;
    }

    if (errno == EEXIST) {
        log_info("output directory already exists: generated");
        return;
    }

    log_error("failed to create output directory generated: errno=%d", errno);
}


static void fmt_i64(long long value, char *out, size_t n)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%lld", value);
    size_t len = strlen(tmp);
    size_t commas = (len > 0) ? (len - 1) / 3 : 0;
    if (len + commas + 1 > n) {
        snprintf(out, n, "%lld", value);
        return;
    }

    size_t i = len;
    size_t j = len + commas;
    out[j--] = '\0';
    int group = 0;
    while (i > 0) {
        out[j--] = tmp[--i];
        if (++group == 3 && i > 0) {
            out[j--] = ',';
            group = 0;
        }
    }
}

static int write_overview(const GitStats *s)
{
    char *tpl = read_file(TEMPLATE_OVERVIEW);
    if (!tpl) return -1;

    char stars[32], forks[32], contrib[32], lines_changed[32], views[32], repos[32];
    fmt_i64(s->stars, stars, sizeof(stars));
    fmt_i64(s->forks, forks, sizeof(forks));
    fmt_i64(s->total_contributions, contrib, sizeof(contrib));
    fmt_i64(s->additions + s->deletions, lines_changed, sizeof(lines_changed));
    fmt_i64(s->views, views, sizeof(views));
    fmt_i64(s->repos, repos, sizeof(repos));

    char *v1 = replace_all(tpl, "{{ name }}", s->display_name);
    char *v2 = replace_all(v1, "{{ stars }}", stars);
    char *v3 = replace_all(v2, "{{ forks }}", forks);
    char *v4 = replace_all(v3, "{{ contributions }}", contrib);
    char *v5 = replace_all(v4, "{{ lines_changed }}", lines_changed);
    char *v6 = replace_all(v5, "{{ views }}", views);
    char *out = replace_all(v6, "{{ repos }}", repos);

    free(tpl);
    free(v1);
    free(v2);
    free(v3);
    free(v4);
    free(v5);
    free(v6);

    if (!out) {
        log_error("failed to render overview template");
        return -1;
    }

    int rc = write_text_file("generated/overview.svg", out);
    free(out);
    return rc;
}

static int write_languages(const GitStats *s)
{
    char *tpl = read_file(TEMPLATE_LANGUAGES);
    if (!tpl) return -1;

    StrBuf progress = {0};
    StrBuf list = {0};

    size_t n = s->lang_count;
    for (size_t i = 0; i < n; ++i) {
        const char *raw_color = s->langs[i].color;
        const char *color =
                (raw_color[0] != '\0' && strcmp(raw_color, "null") != 0)
                        ? raw_color
                        : DEFAULT_LANGUAGE_COLOR;
        if (sb_appendf(&progress,
                       "<span style='width: %.4f%%; background-color: %s'></span>",
                       s->langs[i].percent,
                       color)
                != 0) {
            sb_free(&progress);
            sb_free(&list);
            free(tpl);
            log_error("failed to build language progress template content");
            return -1;
        }


        if (sb_appendf(&list,
                       "<li style='animation-delay: %zums'><span class='dot' style='color: %s'>•</span><span class='lang'>%s</span><span class='percent'>%.2f%%</span></li>",
                       (i % 6) * 120,
                       color,
                       s->langs[i].name,
                       s->langs[i].percent)
                != 0) {
            sb_free(&progress);
            sb_free(&list);
            free(tpl);
            log_error("failed to build language list template content");
            return -1;
        }
    }

    log_info("rendering languages template with %zu language entries", n);

    char *v1 = replace_all(tpl, "{{ progress }}", progress.data ? progress.data : "");
    char *out = replace_all(v1, "{{ lang_list }}", list.data ? list.data : "");

    sb_free(&progress);
    sb_free(&list);
    free(tpl);
    free(v1);

    if (!out) {
        log_error("failed to render languages template");
        return -1;
    }

    int rc = write_text_file("generated/languages.svg", out);
    free(out);
    return rc;
}

int write_svgs(const GitStats *stats)
{
    ensure_generated_dir();
    if (write_overview(stats) != 0) return -1;
    if (write_languages(stats) != 0) return -1;
    return 0;
}
