#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

static FILE            *log_fp    = NULL;
static pthread_mutex_t  log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void get_timestamp(char *buf, size_t sz) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S", tm_info);
}

void logger_init(const char *filename) {
    log_fp = fopen(filename, "a");
    if (!log_fp)
        perror("logger_init: fopen");
}

void logger_close(void) {
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}

static void write_log(const char *level, const char *ip, int port, const char *msg) {
    char ts[32];
    get_timestamp(ts, sizeof(ts));

    pthread_mutex_lock(&log_mutex);
    if (ip) {
        printf("[%s][%s] %s:%d - %s\n", ts, level, ip, port, msg);
        if (log_fp) {
            fprintf(log_fp, "[%s][%s] %s:%d - %s\n", ts, level, ip, port, msg);
            fflush(log_fp);
        }
    } else {
        printf("[%s][%s] %s\n", ts, level, msg);
        if (log_fp) {
            fprintf(log_fp, "[%s][%s] %s\n", ts, level, msg);
            fflush(log_fp);
        }
    }
    pthread_mutex_unlock(&log_mutex);
}

void log_request(const char *ip, int port, const char *msg) {
    write_log("REQUEST",  ip, port, msg);
}

void log_response(const char *ip, int port, const char *msg) {
    write_log("RESPONSE", ip, port, msg);
}

void log_info(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write_log("INFO", NULL, 0, buf);
}

void log_error(const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write_log("ERROR", NULL, 0, buf);
}
