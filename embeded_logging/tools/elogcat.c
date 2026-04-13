/**
 * @file elogcat.c
 * @brief elogcat — 日志查看 CLI 工具 (对标 Android logcat)
 *
 * 用法:
 *   elogcat [-d] [-t N] [-s TAG] [--pid PID] [-v format] [-B] [-c] [-g] [-G SIZE]
 *
 * 连接 elogd reader socket (SOCK_SEQPACKET), 接收二进制日志并格式化输出。
 */

#include "elog_def.h"
#include "elog_format.h"
#include "elog_config.h"
#include "elogd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

/* ===== 命令行配置 ===== */

static struct {
    int   tail;           /* -t: 最近 N 条, -1 = 全部 */
    bool  dump;           /* -d: dump 后退出 */
    char  tag_filter[ELOG_MAX_TAG_LEN];  /* -s: tag 过滤 */
    bool  has_tag_filter;
    uint16_t pid_filter;  /* --pid: PID 过滤, 0 = 不过滤 */
    bool  binary;         /* -B: 二进制输出 */
    bool  color;          /* -v color/brief */
    bool  clear;          /* -c: 清空 */
    bool  get_size;       /* -g: 获取 buffer 大小 */
    uint32_t set_size;    /* -G: 设置 buffer 大小, 0 = 不设置 */
} g_cfg;

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

static void usage(const char* prog) {
    printf("Usage: %s [options]\n"
           "Options:\n"
           "  -d            Dump and exit\n"
           "  -t N          Show last N entries (default: all)\n"
           "  -s TAG        Filter by tag\n"
           "  --pid PID     Filter by PID\n"
           "  -v format     Output format: color (default), brief\n"
           "  -B            Binary output\n"
           "  -c            Clear log buffer\n"
           "  -g            Get buffer size\n"
           "  -G SIZE       Set buffer size\n"
           "  -h            Show this help\n",
           prog);
}

static void parse_args(int argc, char* argv[]) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.tail = 0;  /* 0 = 全部 */

    static struct option long_opts[] = {
        { "pid", required_argument, NULL, 'P' },
        { "help", no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "dt:s:v:BcgG:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd': g_cfg.dump = true; break;
        case 't': g_cfg.tail = atoi(optarg); break;
        case 's':
            strncpy(g_cfg.tag_filter, optarg, ELOG_MAX_TAG_LEN - 1);
            g_cfg.tag_filter[ELOG_MAX_TAG_LEN - 1] = '\0';
            g_cfg.has_tag_filter = true;
            break;
        case 'P': g_cfg.pid_filter = (uint16_t)atoi(optarg); break;
        case 'v':
            if (strcmp(optarg, "brief") == 0) g_cfg.color = false;
            else g_cfg.color = true;
            break;
        case 'B': g_cfg.binary = true; break;
        case 'c': g_cfg.clear = true; break;
        case 'g': g_cfg.get_size = true; break;
        case 'G': g_cfg.set_size = (uint32_t)atoi(optarg); break;
        case 'h': usage(argv[0]); exit(0);
        default:  usage(argv[0]); exit(1);
        }
    }
}

/* ===== cmd socket 操作 ===== */

static int cmd_send(const char* cmd, char* resp, size_t resp_len) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ELOG_DAEMON_CMD_SOCK, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    /* 发送命令 */
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%s\n", cmd);
    if (write(fd, buf, (size_t)n) < 0) {
        close(fd);
        return -1;
    }

    /* 读取响应 */
    if (resp && resp_len > 0) {
        ssize_t r = read(fd, resp, resp_len - 1);
        if (r > 0) resp[r] = '\0';
    }

    close(fd);
    return 0;
}

/* ===== 格式化输出 ===== */

static void print_entry(const uint8_t* data, size_t len) {
    if (len < sizeof(elog_msg_header_t)) return;

    const elog_msg_header_t* hdr = (const elog_msg_header_t*)data;
    const char* tag = (const char*)(data + sizeof(elog_msg_header_t));
    const char* msg = (const char*)(data + sizeof(elog_msg_header_t) + hdr->tag_len);

    /* 确保字符串以 NUL 结尾 */
    char tag_buf[ELOG_MAX_TAG_LEN + 1];
    memcpy(tag_buf, tag, hdr->tag_len);
    tag_buf[hdr->tag_len] = '\0';

    /* tag 过滤 */
    if (g_cfg.has_tag_filter && strcmp(tag_buf, g_cfg.tag_filter) != 0) return;

    if (g_cfg.binary) {
        fwrite(data, 1, len, stdout);
        return;
    }

    /* 文本格式化 */
    elog_format_ctx_t fmt;
    elog_format_text(&fmt, hdr, tag_buf, msg);
    if (fmt.len > 0) {
        fwrite(fmt.buf, 1, (size_t)fmt.len, stdout);
    }
}

/* ===== 主循环 ===== */

static int run_logcat(void) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        fprintf(stderr, "elogcat: socket failed: %s\n", strerror(errno));
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ELOG_DAEMON_READER_SOCK, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "elogcat: connect to %s failed: %s\n",
                ELOG_DAEMON_READER_SOCK, strerror(errno));
        fprintf(stderr, "elogcat: is elogd running?\n");
        close(fd);
        return 1;
    }

    /* 发送 read request */
    elog_read_request_t req;
    memset(&req, 0, sizeof(req));
    req.version = ELOG_READ_PROTOCOL_VERSION;
    req.tail = 1;  /* 从尾部开始 */
    req.count = (g_cfg.dump || g_cfg.tail > 0) ? (uint32_t)g_cfg.tail : 0;
    req.min_level = 0;
    req.pid_filter = g_cfg.pid_filter;
    req.timeout_ms = g_cfg.dump ? 100 : 0;  /* dump 模式 100ms 超时 */

    if (send(fd, &req, sizeof(req), 0) < 0) {
        fprintf(stderr, "elogcat: send request failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    /* 接收日志 */
    uint8_t buf[sizeof(elog_msg_header_t) + ELOG_MAX_TAG_LEN + ELOG_MAX_MSG_LEN];

    while (g_running) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            if (g_cfg.dump) break;
            if (n == 0) break;
            if (errno == EINTR) continue;
            break;
        }

        print_entry(buf, (size_t)n);
        fflush(stdout);
    }

    close(fd);
    return 0;
}

int main(int argc, char* argv[]) {
    parse_args(argc, argv);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* cmd 操作 */
    if (g_cfg.clear) {
        if (cmd_send("clear", NULL, 0) < 0) {
            fprintf(stderr, "elogcat: failed to connect to elogd\n");
            return 1;
        }
        return 0;
    }

    if (g_cfg.get_size) {
        char resp[256];
        if (cmd_send("stats", resp, sizeof(resp)) < 0) {
            fprintf(stderr, "elogcat: failed to connect to elogd\n");
            return 1;
        }
        printf("%s", resp);
        return 0;
    }

    if (g_cfg.set_size > 0) {
        char cmd[64], resp[64];
        snprintf(cmd, sizeof(cmd), "setBufSize %u\n", g_cfg.set_size);
        if (cmd_send(cmd, resp, sizeof(resp)) < 0) {
            fprintf(stderr, "elogcat: failed to connect to elogd\n");
            return 1;
        }
        printf("%s", resp);
        return 0;
    }

    /* 日志读取 */
    return run_logcat();
}
