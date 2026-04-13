/**
 * @file elogd_client.c
 * @brief elogd 客户端库 — 通过 Unix SOCK_DGRAM 发送日志到 elogd 守护进程
 */

#include "elogd.h"
#include "elog_def.h"
#include "elog_port.h"
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

/* 客户端默认 socket 路径 (daemon 进程会用自己的定义覆盖) */
#ifndef ELOGD_IS_DAEMON
const char* g_daemon_write_sock  = ELOG_DAEMON_SOCK_PATH;
const char* g_daemon_cmd_sock    = ELOG_DAEMON_CMD_SOCK;
const char* g_daemon_reader_sock = ELOG_DAEMON_READER_SOCK;
#endif

static int g_elogd_fd = -1;
static elog_mutex_t g_elogd_lock;

int elogd_client_init(void) {
    elog_mutex_lock(&g_elogd_lock);

    if (g_elogd_fd >= 0) {
        elog_mutex_unlock(&g_elogd_lock);
        return ELOG_OK;
    }

    g_elogd_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (g_elogd_fd < 0) {
        elog_mutex_unlock(&g_elogd_lock);
        return ELOG_ERR_NOMEM;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_daemon_write_sock, sizeof(addr.sun_path) - 1);

    /* 连接可能失败 (daemon 未启动), 但 SOCK_DGRAM 连接失败不影响后续 sendto */
    connect(g_elogd_fd, (struct sockaddr*)&addr, sizeof(addr));

    elog_mutex_unlock(&g_elogd_lock);
    return ELOG_OK;
}

void elogd_client_destroy(void) {
    elog_mutex_lock(&g_elogd_lock);
    if (g_elogd_fd >= 0) {
        close(g_elogd_fd);
        g_elogd_fd = -1;
    }
    elog_mutex_unlock(&g_elogd_lock);
}

int elogd_client_send(const elog_msg_header_t* hdr, const char* tag, const char* msg) {
    if (!hdr) return ELOG_ERR_PARAM;

    elog_mutex_lock(&g_elogd_lock);

    if (g_elogd_fd < 0) {
        elog_mutex_unlock(&g_elogd_lock);
        return ELOG_ERR_NOT_INIT;
    }

    /* 构造 datagram: header + tag + msg */
    uint8_t buf[sizeof(elog_msg_header_t) + ELOG_MAX_TAG_LEN + ELOG_MAX_MSG_LEN];
    memcpy(buf, hdr, sizeof(elog_msg_header_t));

    uint16_t tag_len = tag ? (uint16_t)ELOG_MIN(strlen(tag), ELOG_MAX_TAG_LEN - 1) : 0;
    uint16_t msg_len = msg ? (uint16_t)ELOG_MIN(strlen(msg), ELOG_MAX_MSG_LEN - 1) : 0;

    if (tag_len > 0) memcpy(buf + sizeof(elog_msg_header_t), tag, tag_len);
    if (msg_len > 0) memcpy(buf + sizeof(elog_msg_header_t) + tag_len, msg, msg_len);

    size_t total = sizeof(elog_msg_header_t) + tag_len + msg_len;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_daemon_write_sock, sizeof(addr.sun_path) - 1);

    ssize_t sent = sendto(g_elogd_fd, buf, total, 0,
                          (struct sockaddr*)&addr, sizeof(addr));

    if (sent < 0) {
        /* 发送失败 — daemon 可能已退出, 关闭 fd 下次重连 */
        close(g_elogd_fd);
        g_elogd_fd = -1;
        elog_mutex_unlock(&g_elogd_lock);
        return ELOG_ERR_BUSY;
    }

    elog_mutex_unlock(&g_elogd_lock);
    return ELOG_OK;
}

bool elogd_client_is_connected(void) {
    elog_mutex_lock(&g_elogd_lock);
    int fd = g_elogd_fd;
    elog_mutex_unlock(&g_elogd_lock);
    return fd >= 0;
}

int elogd_client_send_binary(const elog_msg_header_t* hdr, const char* tag,
                              const uint8_t* msg, uint16_t msg_len) {
    if (!hdr) return ELOG_ERR_PARAM;

    elog_mutex_lock(&g_elogd_lock);
    if (g_elogd_fd < 0) {
        elog_mutex_unlock(&g_elogd_lock);
        return ELOG_ERR_NOT_INIT;
    }

    uint8_t buf[sizeof(elog_msg_header_t) + ELOG_MAX_TAG_LEN + ELOG_MAX_MSG_LEN];
    memcpy(buf, hdr, sizeof(elog_msg_header_t));

    uint16_t tag_len = tag ? (uint16_t)ELOG_MIN(strlen(tag), ELOG_MAX_TAG_LEN - 1) : 0;
    if (tag_len > 0) memcpy(buf + sizeof(elog_msg_header_t), tag, tag_len);
    if (msg && msg_len > 0) memcpy(buf + sizeof(elog_msg_header_t) + tag_len, msg, msg_len);

    size_t total = sizeof(elog_msg_header_t) + tag_len + msg_len;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_daemon_write_sock, sizeof(addr.sun_path) - 1);

    ssize_t sent = sendto(g_elogd_fd, buf, total, 0,
                          (struct sockaddr*)&addr, sizeof(addr));
    if (sent < 0) {
        close(g_elogd_fd);
        g_elogd_fd = -1;
        elog_mutex_unlock(&g_elogd_lock);
        return ELOG_ERR_BUSY;
    }

    elog_mutex_unlock(&g_elogd_lock);
    return ELOG_OK;
}
