/**
 * @file elog_transport_stdout.c
 * @brief StdoutTransport 实现 + TransportRegistry 实现
 */

#include "elog_transport.h"
#include <unistd.h>
#include <string.h>

/* ===== TransportRegistry ===== */

void elog_transport_registry_init(elog_transport_registry_t* r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    pthread_mutex_init(&r->lock, NULL);
    r->initialized = true;
}

int elog_transport_register(elog_transport_registry_t* r, elog_transport_t* t) {
    if (!r || !t) return ELOG_ERR_PARAM;
    if (r->count >= ELOG_MAX_TRANSPORTS) return ELOG_ERR_FULL;

    pthread_mutex_lock(&r->lock);
    r->transports[r->count++] = t;
    pthread_mutex_unlock(&r->lock);
    return ELOG_OK;
}

int elog_transport_unregister(elog_transport_registry_t* r, elog_transport_t* t) {
    if (!r || !t) return ELOG_ERR_PARAM;

    pthread_mutex_lock(&r->lock);
    for (uint8_t i = 0; i < r->count; i++) {
        if (r->transports[i] == t) {
            /* 移动后续元素 */
            for (uint8_t j = i; j < r->count - 1; j++) {
                r->transports[j] = r->transports[j + 1];
            }
            r->count--;
            pthread_mutex_unlock(&r->lock);
            return ELOG_OK;
        }
    }
    pthread_mutex_unlock(&r->lock);
    return ELOG_ERR_PARAM;
}

void elog_transport_dispatch(elog_transport_registry_t* r, const uint8_t* data, size_t len) {
    if (!r || !data || len == 0) return;

    pthread_mutex_lock(&r->lock);
    for (uint8_t i = 0; i < r->count; i++) {
        elog_transport_t* t = r->transports[i];
        if (t && t->write && t->is_open && t->is_open(t)) {
            t->write(t, data, len);
        }
    }
    pthread_mutex_unlock(&r->lock);
}

void elog_transport_flush_all(elog_transport_registry_t* r) {
    if (!r) return;

    pthread_mutex_lock(&r->lock);
    for (uint8_t i = 0; i < r->count; i++) {
        elog_transport_t* t = r->transports[i];
        if (t && t->flush && t->is_open && t->is_open(t)) {
            t->flush(t);
        }
    }
    pthread_mutex_unlock(&r->lock);
}

void elog_transport_registry_destroy(elog_transport_registry_t* r) {
    if (!r) return;
    pthread_mutex_destroy(&r->lock);
    r->count = 0;
    r->initialized = false;
}

/* ===== StdoutTransport ===== */

static int stdout_open(elog_transport_t* self) {
    elog_stdout_transport_t* t = (elog_stdout_transport_t*)self;
    if (!t) return ELOG_ERR_PARAM;
    t->is_open_ = true;
    return ELOG_OK;
}

static void stdout_close(elog_transport_t* self) {
    elog_stdout_transport_t* t = (elog_stdout_transport_t*)self;
    if (!t) return;
    t->is_open_ = false;
}

static int stdout_write(elog_transport_t* self, const uint8_t* data, size_t len) {
    elog_stdout_transport_t* t = (elog_stdout_transport_t*)self;
    if (!t || !data) return ELOG_ERR_PARAM;
    ssize_t ret = write(t->fd, data, len);
    return (ret >= 0) ? (int)ret : ELOG_ERR_FULL;
}

static int stdout_flush(elog_transport_t* self) {
    (void)self;
    /* POSIX stdout 通常行缓冲，fsync 不适用于 stdout */
    return ELOG_OK;
}

static bool stdout_is_open(elog_transport_t* self) {
    elog_stdout_transport_t* t = (elog_stdout_transport_t*)self;
    return t ? t->is_open_ : false;
}

static const char* stdout_name(elog_transport_t* self) {
    (void)self;
    return "stdout";
}

int elog_stdout_transport_init(elog_stdout_transport_t* t) {
    return elog_stdout_transport_init_fd(t, STDOUT_FILENO);
}

int elog_stdout_transport_init_fd(elog_stdout_transport_t* t, int fd) {
    if (!t) return ELOG_ERR_PARAM;
    memset(t, 0, sizeof(*t));
    t->fd = fd;
    t->is_open_ = false;

    t->base.open    = stdout_open;
    t->base.close   = stdout_close;
    t->base.write   = stdout_write;
    t->base.flush   = stdout_flush;
    t->base.is_open = stdout_is_open;
    t->base.name    = stdout_name;

    return ELOG_OK;
}
