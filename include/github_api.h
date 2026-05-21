#ifndef GITHUB_API_H
#define GITHUB_API_H

#include <stddef.h>

char *github_get(const char *url, const char *token);
char *github_get_with_accept(const char *url, const char *token, const char *accept);
char *github_graphql(const char *token, const char *query);
int github_paginated_repos(const char *user, const char *token, char ***pages, size_t *count);

#endif
