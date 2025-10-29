#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

typedef struct {
    LogLevel level;
    FILE *file;
} Logger;

void log_init(const char *filename, LogLevel level);
void log_close(void);
void log_write(LogLevel level, const char *fmt, ...);

#define log_debug(...) log_write(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...)  log_write(LOG_LEVEL_INFO,  __VA_ARGS__)
#define log_warn(...)  log_write(LOG_LEVEL_WARN,  __VA_ARGS__)
#define log_error(...) log_write(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif
