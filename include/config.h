#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#define MAX_STR 256

typedef struct {
    char token[MAX_STR];
    char user[MAX_STR];
    bool count_forks;
} AppConfig;

int load_config(AppConfig *cfg);

#endif
