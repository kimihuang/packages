/**
 * @file elogd.c
 * @brief elogd 守护进程入口
 *
 * 线程模型 (对标 Android logd):
 *   main        — init → 启动 4 detached threads → pause()
 *   listener    — SOCK_DGRAM recvmsg 循环, 按 log_id 路由到对应 ring buffer
 *   cmd         — SOCK_STREAM accept 循环, 处理管理命令
 *   flusher     — condvar wait → 遍历所有 buffer flush → file transport
 *   reader      — SOCK_SEQPACKET accept, per-client push 线程
 */

#define ELOGD_IS_DAEMON

#include "elogd.h"
#include "elog_buf.h"
#include "elog_prune.h"
#include "elog_config.h"
#include "elog_def.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <getopt.h>

/* ===== 多 Buffer 管理 ===== */

static elog_ring_buf_t s_daemon_rbs[ELOG_ID_MAX];
static elog_prune_t    s_daemon_prunes[ELOG_ID_MAX];
static size_t          s_buf_sizes[ELOG_ID_MAX];

/* 默认容量 (参考 Android logd) */
static const size_t k_default_buf_sizes[ELOG_ID_MAX] = {
    [ELOG_ID_MAIN]   = 256 * 1024,
    [ELOG_ID_RADIO]  = 64  * 1024,
    [ELOG_ID_EVENTS] = 256 * 1024,
    [ELOG_ID_SYSTEM] = 64  * 1024,
    [ELOG_ID_CRASH]  = 1024 * 1024,
    [ELOG_ID_KERNEL] = 64  * 1024,
};

elog_ring_buf_t* g_daemon_rb = &s_daemon_rbs[0];  /* 向后兼容 */
volatile bool g_daemon_running = false;

/* 公共 API */
elog_ring_buf_t* elogd_get_buf(elog_id_t id) {
    if (id >= ELOG_ID_MAX) id = ELOG_ID_MAIN;
    return &s_daemon_rbs[id];
}

size_t elogd_get_buf_size(elog_id_t id) {
    if (id >= ELOG_ID_MAX) id = ELOG_ID_MAIN;
    return s_buf_sizes[id];
}

/* ===== Socket 路径 (运行时可覆盖) ===== */

const char* g_daemon_write_sock  = ELOG_DAEMON_SOCK_PATH;
const char* g_daemon_cmd_sock    = ELOG_DAEMON_CMD_SOCK;
const char* g_daemon_reader_sock = ELOG_DAEMON_READER_SOCK;

/* ===== 线程 ===== */

static pthread_t s_listener_tid;
static pthread_t s_cmd_tid;
static pthread_t s_flusher_tid;
static pthread_t s_reader_tid;

extern void* elogd_listener_thread(void* arg);
extern void* elogd_cmd_thread(void* arg);
extern void* elogd_flusher_thread(void* arg);
extern void* elogd_reader_thread(void* arg);

static void signal_handler(int sig) {
    (void)sig;
    g_daemon_running = false;
}

int elogd_run(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("elogd: starting with %d log buffers...\n", ELOG_ID_MAX);

    /* 初始化所有 ring buffer */
    for (int i = 0; i < ELOG_ID_MAX; i++) {
        memcpy(s_buf_sizes, k_default_buf_sizes, sizeof(k_default_buf_sizes));
    }

    for (int i = 0; i < ELOG_ID_MAX; i++) {
        int ret = elog_ring_buf_init(&s_daemon_rbs[i], s_buf_sizes[i]);
        if (ret != ELOG_OK) {
            fprintf(stderr, "elogd: ring buf[%s] init failed (%d)\n",
                    elogd_buf_name((elog_id_t)i), ret);
            return ret;
        }
#if ELOG_PRUNE_ENABLE
        elog_prune_init(&s_daemon_prunes[i], ELOG_PRUNE_THRESHOLD_PCT);
        elog_prune_load_rules(&s_daemon_prunes[i], "~elogd ~crash");
        elog_ring_buf_set_prune(&s_daemon_rbs[i], &s_daemon_prunes[i]);
#endif
        printf("elogd:   %s  %zuKB\n", elogd_buf_name((elog_id_t)i), s_buf_sizes[i] / 1024);
    }

    g_daemon_running = true;

    pthread_create(&s_listener_tid, NULL, elogd_listener_thread, NULL);
    pthread_create(&s_cmd_tid, NULL, elogd_cmd_thread, NULL);
    pthread_create(&s_flusher_tid, NULL, elogd_flusher_thread, NULL);
    pthread_create(&s_reader_tid, NULL, elogd_reader_thread, NULL);

    pthread_detach(s_listener_tid);
    pthread_detach(s_cmd_tid);
    pthread_detach(s_flusher_tid);
    pthread_detach(s_reader_tid);

    printf("elogd: ready\n");

    while (g_daemon_running) {
        pause();
    }

    printf("elogd: shutting down...\n");
    usleep(100000);

    for (int i = 0; i < ELOG_ID_MAX; i++) {
        elog_ring_buf_destroy(&s_daemon_rbs[i]);
    }

    printf("elogd: stopped\n");
    return ELOG_OK;
}

void elogd_stop(void) {
    g_daemon_running = false;
}

int main(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "w:c:r:h")) != -1) {
        switch (opt) {
        case 'w': g_daemon_write_sock  = optarg; break;
        case 'c': g_daemon_cmd_sock    = optarg; break;
        case 'r': g_daemon_reader_sock = optarg; break;
        case 'h':
            printf("Usage: elogd [-w write_sock] [-c cmd_sock] [-r reader_sock]\n");
            return 0;
        default:
            fprintf(stderr, "elogd: unknown option -%c\n", opt);
            return 1;
        }
    }
    return elogd_run();
}
