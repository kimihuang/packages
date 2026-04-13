/**
 * @file elogd_listener.c
 * @brief LogListener — 通过 SOCK_DGRAM 接收客户端日志，写入 ring buffer
 */

#define _GNU_SOURCE  /* struct ucred, SCM_CREDENTIALS */

#include "elogd.h"
#include "elog_buf.h"
#include "elog_def.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

/* 共享状态 (由 elogd.c 定义) */
extern volatile bool g_daemon_running;

void* elogd_listener_thread(void* arg) {
    (void)arg;
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("elogd_listener: socket");
        return NULL;
    }

    /* 确保旧 socket 文件不存在 */
    unlink(g_daemon_write_sock);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_daemon_write_sock, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("elogd_listener: bind");
        close(fd);
        return NULL;
    }

    while (g_daemon_running) {
        uint8_t buf[sizeof(elog_msg_header_t) + ELOG_MAX_TAG_LEN + ELOG_MAX_MSG_LEN];
        struct iovec iov;
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        /* SCM_CREDENTIALS buffer (可选, 用于 UID 验证) */
        char cmsgbuf[CMSG_SPACE(sizeof(struct ucred))] = {0};
        struct cmsghdr* cmsg = (struct cmsghdr*)cmsgbuf;
        cmsg->cmsg_len = CMSG_LEN(sizeof(struct ucred));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_CREDENTIALS;
        msg.msg_control = cmsgbuf;
        msg.msg_controllen = sizeof(cmsgbuf);

        ssize_t n = recvmsg(fd, &msg, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("elogd_listener: recvmsg");
            continue;
        }

        if (n < (ssize_t)sizeof(elog_msg_header_t)) continue;

        /* 解析 header */
        elog_msg_header_t hdr;
        memcpy(&hdr, buf, sizeof(hdr));

        /* 将 tag 和 msg 复制到独立 buffer 并 null-terminate */
        char tag_buf[ELOG_MAX_TAG_LEN + 1] = {0};
        char msg_buf[ELOG_MAX_MSG_LEN + 1] = {0};

        size_t tag_off = sizeof(elog_msg_header_t);
        size_t msg_off = tag_off + hdr.tag_len;

        if (hdr.tag_len > 0 && hdr.tag_len <= ELOG_MAX_TAG_LEN) {
            memcpy(tag_buf, buf + tag_off, hdr.tag_len);
        }
        if (hdr.msg_len > 0 && msg_off + hdr.msg_len <= sizeof(buf)) {
            memcpy(msg_buf, buf + msg_off, hdr.msg_len);
        }

        /* 按 log_id 路由到对应 buffer (非法 ID 降级到 MAIN) */
        elog_id_t id = (elog_id_t)hdr.log_id;
        if (id >= ELOG_ID_MAX) id = ELOG_ID_MAIN;

        elog_ring_buf_t* target = elogd_get_buf(id);
        if (target) {
            int ret = elog_ring_buf_log_from(&target->base,
                id, (elog_level_t)hdr.level,
                hdr.pid, hdr.tid, hdr.line, tag_buf, msg_buf);
            if (ret != ELOG_OK) {
                /* prune drop 或 buffer full, 静默忽略 */
            }
        }
    }

    close(fd);
    unlink(g_daemon_write_sock);    return NULL;
}
