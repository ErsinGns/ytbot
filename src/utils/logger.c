#include "logger.h"
#include <stdlib.h>
#include <string.h>

static Logger global_logger = { .level = LOG_LEVEL_INFO, .file = NULL };

void log_init(const char *filename, LogLevel level) {
    global_logger.level = level;

    if (filename && strlen(filename) > 0) {
        global_logger.file = fopen(filename, "a");
        if (!global_logger.file) {
            fprintf(stderr, "Logger: '%s' dosyası açılamadı, stdout kullanılacak.\n", filename);
            global_logger.file = stdout;
        }
    } else {
        global_logger.file = stdout;
    }

    time_t now = time(NULL);
    fprintf(global_logger.file, "\n--- Log başlatıldı: %s", ctime(&now));
    fflush(global_logger.file);
}

void log_close(void) {
    if (global_logger.file && global_logger.file != stdout) {
        fclose(global_logger.file);
        global_logger.file = NULL;
    }
}

void log_write(LogLevel level, const char *fmt, ...) {
    if (level < global_logger.level) return;

    const char *level_str;
    switch (level) {
        case LOG_LEVEL_DEBUG: level_str = "DEBUG"; break;
        case LOG_LEVEL_INFO:  level_str = "INFO "; break;
        case LOG_LEVEL_WARN:  level_str = "WARN "; break;
        case LOG_LEVEL_ERROR: level_str = "ERROR"; break;
        default: level_str = "LOG  "; break;
    }

    // Zaman damgası
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    // Log çıktısı
    FILE *out = global_logger.file ? global_logger.file : stdout;
    fprintf(out, "[%s] [%s] ", time_str, level_str);

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fprintf(out, "\n");
    fflush(out);
}
