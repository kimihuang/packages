/**
 * @file elog_port_linux.c
 * @brief Port 层 Linux 实现
 *
 * 基于 POSIX API: pthread, time, getpid, gettid, __atomic。
 */

#include "elog_port.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>

/* ===== 同步原语 ===== */

void elog_mutex_init(elog_mutex_t* m) {
    if (!m) return;
    pthread_mutex_t* mtx = (pthread_mutex_t*)m->_opaque;
    pthread_mutex_init(mtx, NULL);
}

void elog_mutex_destroy(elog_mutex_t* m) {
    if (!m) return;
    pthread_mutex_t* mtx = (pthread_mutex_t*)m->_opaque;
    pthread_mutex_destroy(mtx);
}

void elog_mutex_lock(elog_mutex_t* m) {
    if (!m) return;
    pthread_mutex_t* mtx = (pthread_mutex_t*)m->_opaque;
    pthread_mutex_lock(mtx);
}

void elog_mutex_unlock(elog_mutex_t* m) {
    if (!m) return;
    pthread_mutex_t* mtx = (pthread_mutex_t*)m->_opaque;
    pthread_mutex_unlock(mtx);
}

void elog_cond_init(elog_cond_t* c) {
    if (!c) return;
    pthread_cond_t* cv = (pthread_cond_t*)c->_opaque;
    pthread_cond_init(cv, NULL);
}

void elog_cond_destroy(elog_cond_t* c) {
    if (!c) return;
    pthread_cond_t* cv = (pthread_cond_t*)c->_opaque;
    pthread_cond_destroy(cv);
}

void elog_cond_signal(elog_cond_t* c) {
    if (!c) return;
    pthread_cond_t* cv = (pthread_cond_t*)c->_opaque;
    pthread_cond_signal(cv);
}

void elog_cond_wait(elog_cond_t* c, elog_mutex_t* m) {
    if (!c || !m) return;
    pthread_cond_t* cv = (pthread_cond_t*)c->_opaque;
    pthread_mutex_t* mtx = (pthread_mutex_t*)m->_opaque;
    pthread_cond_wait(cv, mtx);
}

int elog_cond_timedwait(elog_cond_t* c, elog_mutex_t* m, int timeout_ms) {
    if (!c || !m) return ELOG_ERR_PARAM;
    if (timeout_ms <= 0) return ELOG_ERR_BUSY;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec  += 1;
        ts.tv_nsec -= 1000000000L;
    }

    pthread_cond_t* cv = (pthread_cond_t*)c->_opaque;
    pthread_mutex_t* mtx = (pthread_mutex_t*)m->_opaque;
    int rc = pthread_cond_timedwait(cv, mtx, &ts);
    return (rc == ETIMEDOUT) ? ELOG_ERR_BUSY : ELOG_OK;
}

/* ===== 时间 ===== */

uint32_t elog_port_now(void) {
    return (uint32_t)time(NULL);
}

void elog_port_localtime(uint32_t timestamp,
                         int* hour, int* minute, int* second,
                         int* day, int* month) {
    time_t t = (time_t)timestamp;
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    if (hour)   *hour   = tm_buf.tm_hour;
    if (minute) *minute = tm_buf.tm_min;
    if (second) *second = tm_buf.tm_sec;
    if (day)    *day    = tm_buf.tm_mday;
    if (month)  *month  = tm_buf.tm_mon + 1;
}

/* ===== 进程/线程 ID ===== */

uint16_t elog_port_getpid(void) {
    return (uint16_t)getpid();
}

uint16_t elog_port_gettid(void) {
    return (uint16_t)(pid_t)syscall(SYS_gettid);
}

/* ===== 原子操作 ===== */

uint32_t elog_port_atomic_inc(volatile uint32_t* val) {
    return __atomic_fetch_add(val, 1, __ATOMIC_SEQ_CST);
}

/* ===== 中断控制 (Linux 不需要) ===== */

elog_isr_state_t elog_port_isr_save(void) {
    return 0;
}

void elog_port_isr_restore(elog_isr_state_t state) {
    (void)state;
}
