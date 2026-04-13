/**
 * @file elogd.h
 * @brief elogd 守护进程公共声明
 */

#ifndef ELOGD_H
#define ELOGD_H

#include "elog_def.h"
#include "elog_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Socket 路径 (可被 config 覆盖) */
#ifndef ELOG_DAEMON_SOCK_PATH
#define ELOG_DAEMON_SOCK_PATH  "/var/run/elogdw"
#endif

#ifndef ELOG_DAEMON_CMD_SOCK
#define ELOG_DAEMON_CMD_SOCK  "/var/run/elogd"
#endif

#ifndef ELOG_DAEMON_READER_SOCK
#define ELOG_DAEMON_READER_SOCK  "/var/run/elogdr"
#endif

/* Daemon 缓冲区大小 (独立于 ELOG_BUFFER_SIZE) */
#ifndef ELOG_DAEMON_BUFFER_SIZE
#define ELOG_DAEMON_BUFFER_SIZE  (256 * 1024)  /* 256KB */
#endif

/* Daemon 日志文件路径 */
#ifndef ELOG_DAEMON_LOG_FILE
#define ELOG_DAEMON_LOG_FILE    "/var/log/elog.log"
#endif

/**
 * 运行 elogd 守护进程 (阻塞调用)
 * @return 正常退出 ELOG_OK, 错误返回负值
 */
int elogd_run(void);

/**
 * 停止 elogd (设置 running=false, 等待线程退出)
 */
void elogd_stop(void);

/* ===== 客户端 API ===== */

/**
 * 初始化 elogd 客户端 (连接 SOCK_DGRAM)
 */
int elogd_client_init(void);

/**
 * 销毁 elogd 客户端
 */
void elogd_client_destroy(void);

/**
 * 发送日志到 elogd
 * @return ELOG_OK 成功, ELOG_ERR_NOT_INIT 未初始化, ELOG_ERR_BUSY 发送失败
 */
int elogd_client_send(const elog_msg_header_t* hdr, const char* tag, const char* msg);

/**
 * 检查客户端是否已连接
 */
bool elogd_client_is_connected(void);

/* ===== Reader 协议 ===== */

#define ELOG_READ_PROTOCOL_VERSION  1

/**
 * elogcat 连接时发送的请求 (SOCK_SEQPACKET 握手)
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t version;       /* 协议版本, 当前 = 1 */
    uint32_t tail;          /* 1 = 从尾部开始, 0 = 从头部开始 */
    uint32_t count;         /* 最多读取条数, 0 = 无限 */
    uint8_t  min_level;     /* 最低级别过滤, 0 = 全部 */
    uint16_t pid_filter;    /* PID 过滤, 0 = 不过滤 */
    uint32_t timeout_ms;    /* 阻塞超时, 0 = 永久阻塞 */
} elog_read_request_t;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* ELOGD_H */
