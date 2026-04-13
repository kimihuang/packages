/**
 * @file elog_transport.h
 * @brief LogTransport 抽象接口 + TransportRegistry 多路分发
 *
 * 借鉴 Android write_to_log() 的双路分发，扩展为 N 路注册表模式。
 * 每个传输目标实现 elog_transport_t vtable，注册到 Registry 后自动分发。
 */

#ifndef ELOG_TRANSPORT_H
#define ELOG_TRANSPORT_H

#include "elog_def.h"
#include "elog_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== LogTransport 抽象接口 ===== */
typedef struct elog_transport {
    int  (*open)(struct elog_transport* self);
    void (*close)(struct elog_transport* self);
    int  (*write)(struct elog_transport* self, const uint8_t* data, size_t len);
    int  (*flush)(struct elog_transport* self);
    bool (*is_open)(struct elog_transport* self);
    const char* (*name)(struct elog_transport* self);
} elog_transport_t;

/* ===== TransportRegistry ===== */
typedef struct {
    elog_transport_t* transports[ELOG_MAX_TRANSPORTS];
    uint8_t           count;
    elog_mutex_t     lock;
    bool              initialized;
} elog_transport_registry_t;

/**
 * 初始化 Registry
 */
void elog_transport_registry_init(elog_transport_registry_t* r);

/**
 * 注册传输目标
 */
int elog_transport_register(elog_transport_registry_t* r, elog_transport_t* t);

/**
 * 注销传输目标
 */
int elog_transport_unregister(elog_transport_registry_t* r, elog_transport_t* t);

/**
 * 分发数据到所有已注册的传输目标
 */
void elog_transport_dispatch(elog_transport_registry_t* r, const uint8_t* data, size_t len);

/**
 * 刷新所有传输目标
 */
void elog_transport_flush_all(elog_transport_registry_t* r);

/**
 * 销毁 Registry
 */
void elog_transport_registry_destroy(elog_transport_registry_t* r);

/* ===== StdoutTransport (Linux 测试用) ===== */
typedef struct {
    elog_transport_t base;
    int  fd;         /* 文件描述符 (默认 STDOUT_FILENO) */
    bool is_open_;
} elog_stdout_transport_t;

/**
 * 初始化 StdoutTransport
 */
int elog_stdout_transport_init(elog_stdout_transport_t* t);

/**
 * 使用指定 fd 初始化
 */
int elog_stdout_transport_init_fd(elog_stdout_transport_t* t, int fd);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_TRANSPORT_H */
