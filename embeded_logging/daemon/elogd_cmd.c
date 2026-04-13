/**
 * @file elogd_cmd.c
 * @brief CmdListener — 通过 SOCK_STREAM 处理管理命令
 *
 * 命令协议 (逐行文本):
 *   clear    — 清空 ring buffer
 *   stats    — 返回统计信息
 *   prune    — 返回当前 prune 规则
 *   exit     — 关闭 daemon
 */

#include "elogd.h"
#include "elog_buf.h"
#include "elog_stats.h"
#include "elog_prune.h"
#include "elog_def.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>

extern elog_ring_buf_t* g_daemon_rb;
extern volatile bool g_daemon_running;

/* 处理单条命令, 返回响应长度 */
static int handle_command(const char* cmd, char* resp, size_t resp_len) {
    if (strncmp(cmd, "clear", 5) == 0) {
        if (g_daemon_rb) {
            elog_ring_buf_clear(&g_daemon_rb->base);
        }
        return snprintf(resp, resp_len, "OK\n");
    }
    if (strncmp(cmd, "stats", 5) == 0) {
        /* 简单统计 */
        int n = 0;
        if (g_daemon_rb) {
            n += snprintf(resp + n, resp_len - (size_t)n,
                          "buffer: capacity=%zu count=%zu wp=%zu rp=%zu\n",
                          g_daemon_rb->buf_capacity, g_daemon_rb->count,
                          g_daemon_rb->write_pos, g_daemon_rb->read_pos);
        }
        if (n == 0) n = snprintf(resp, resp_len, "no buffer\n");
        return n;
    }
    if (strncmp(cmd, "prune", 5) == 0) {
        if (g_daemon_rb && g_daemon_rb->prune) {
            char rules[256];
            elog_prune_serialize_rules(g_daemon_rb->prune, rules, sizeof(rules));
            return snprintf(resp, resp_len, "prune: threshold=%u rules=[%s]\n",
                            g_daemon_rb->prune->threshold_pct, rules);
        }
        return snprintf(resp, resp_len, "prune: not configured\n");
    }
    if (strncmp(cmd, "exit", 4) == 0) {
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

    unlink(ELOG_DAEMON_CMD_SOCK);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ELOG_DAEMON_CMD_SOCK, sizeof(addr.sun_path) - 1);

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
            perror("elogd_cmd: accept");
            continue;
        }

        /* 读取一行命令 */
        char cmd[128];
        ssize_t n = read(client, cmd, sizeof(cmd) - 1);
        if (n > 0) {
            cmd[n] = '\0';
            /* 去除换行 */
            char* nl = strchr(cmd, '\n');
            if (nl) *nl = '\0';
            nl = strchr(cmd, '\r');
            if (nl) *nl = '\0';

            char resp[512];
            int resp_len = handle_command(cmd, resp, sizeof(resp));
            if (resp_len > 0) {
                write(client, resp, (size_t)resp_len);
            }
        }
        close(client);
    }

    close(fd);
    unlink(ELOG_DAEMON_CMD_SOCK);
    return NULL;
}
