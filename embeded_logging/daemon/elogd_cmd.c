/**
 * @file elogd_cmd.c
 * @brief CmdListener — 通过 SOCK_STREAM 处理管理命令
 *
 * 命令协议 (逐行文本):
 *   clear [id]   — 清空指定 buffer (默认清全部)
 *   stats [id]   — 返回指定 buffer 统计 (默认全部)
 *   prune        — 返回当前 prune 规则
 *   exit         — 关闭 daemon
 */

#include "elogd.h"
#include "elog_buf.h"
#include "elog_prune.h"
#include "elog_def.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

extern volatile bool g_daemon_running;

/* 解析可选的 log_id 参数 (数字或名称) */
static elog_id_t parse_log_id(const char* arg) {
    if (!arg || !*arg) return ELOG_ID_MAX;  /* 表示"全部" */

    /* 先尝试数字 */
    char* end;
    long val = strtol(arg, &end, 10);
    if (end != arg && *end == '\0') {
        if (val >= 0 && val < ELOG_ID_MAX) return (elog_id_t)val;
        return ELOG_ID_MAX;
    }

    /* 尝试名称 */
    for (int i = 0; i < ELOG_ID_MAX; i++) {
        if (strcmp(arg, elogd_buf_name((elog_id_t)i)) == 0) return (elog_id_t)i;
    }
    return ELOG_ID_MAX;
}

static int handle_command(const char* cmd, char* resp, size_t resp_len) {
    /* 解析命令和参数 */
    char cmd_name[32] = {0};
    char cmd_arg[64] = {0};
    sscanf(cmd, "%31s %63s", cmd_name, cmd_arg);

    if (strcmp(cmd_name, "clear") == 0) {
        elog_id_t id = parse_log_id(cmd_arg[0] ? cmd_arg : NULL);
        if (id < ELOG_ID_MAX) {
            elog_ring_buf_t* rb = elogd_get_buf(id);
            if (rb) elog_ring_buf_clear(&rb->base);
            return snprintf(resp, resp_len, "OK cleared %s\n", elogd_buf_name(id));
        } else {
            for (int i = 0; i < ELOG_ID_MAX; i++) {
                elog_ring_buf_t* rb = elogd_get_buf((elog_id_t)i);
                if (rb) elog_ring_buf_clear(&rb->base);
            }
            return snprintf(resp, resp_len, "OK cleared all\n");
        }
    }

    if (strcmp(cmd_name, "stats") == 0) {
        elog_id_t id = parse_log_id(cmd_arg[0] ? cmd_arg : NULL);
        int n = 0;

        if (id < ELOG_ID_MAX) {
            /* 单个 buffer 详情 */
            elog_ring_buf_t* rb = elogd_get_buf(id);
            if (rb) {
                n += snprintf(resp + n, resp_len - (size_t)n,
                    "%-8s: capacity=%zu count=%zu wp=%zu rp=%zu\n",
                    elogd_buf_name(id), rb->buf_capacity, rb->count,
                    rb->write_pos, rb->read_pos);
            }
        } else {
            /* 所有 buffer 概要 */
            for (int i = 0; i < ELOG_ID_MAX; i++) {
                elog_ring_buf_t* rb = elogd_get_buf((elog_id_t)i);
                if (rb) {
                    n += snprintf(resp + n, resp_len - (size_t)n,
                        "%-8s: capacity=%zu count=%zu wp=%zu rp=%zu\n",
                        elogd_buf_name(i), rb->buf_capacity, rb->count,
                        rb->write_pos, rb->read_pos);
                }
            }
        }
        if (n == 0) n = snprintf(resp, resp_len, "no buffers\n");
        return n;
    }

    if (strcmp(cmd_name, "prune") == 0) {
        elog_ring_buf_t* rb = elogd_get_buf(ELOG_ID_MAIN);
        if (rb && rb->prune) {
            char rules[256];
            elog_prune_serialize_rules(rb->prune, rules, sizeof(rules));
            return snprintf(resp, resp_len, "prune: threshold=%u rules=[%s]\n",
                            rb->prune->threshold_pct, rules);
        }
        return snprintf(resp, resp_len, "prune: not configured\n");
    }

    if (strcmp(cmd_name, "exit") == 0) {
        g_daemon_running = false;
        return snprintf(resp, resp_len, "OK\n");
    }

    return snprintf(resp, resp_len, "ERR unknown command: %s\n", cmd);
}

void* elogd_cmd_thread(void* arg) {
    (void)arg;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("elogd_cmd: socket");
        return NULL;
    }

    unlink(g_daemon_cmd_sock);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_daemon_cmd_sock, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("elogd_cmd: bind");
        close(fd);
        return NULL;
    }

    if (listen(fd, 4) < 0) {
        perror("elogd_cmd: listen");
        close(fd);
        return NULL;
    }

    while (g_daemon_running) {
        int client = accept(fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            if (!g_daemon_running) break;
            continue;
        }

        char cmd[128];
        ssize_t n = read(client, cmd, sizeof(cmd) - 1);
        if (n > 0) {
            cmd[n] = '\0';
            char* nl = strchr(cmd, '\n'); if (nl) *nl = '\0';
            nl = strchr(cmd, '\r'); if (nl) *nl = '\0';

            char resp[1024];
            int resp_len = handle_command(cmd, resp, sizeof(resp));
            if (resp_len > 0) {
                write(client, resp, (size_t)resp_len);
            }
        }
        close(client);
    }

    close(fd);
    unlink(g_daemon_cmd_sock);
    return NULL;
}
