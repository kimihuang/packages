/**
 * @file elogd.c
 * @brief elogd 守护进程入口
 *
 * 线程模型 (对标 Android logd):
 *   main        — init → 启动 3 detached threads → pause()
 *   listener    — SOCK_DGRAM recvmsg 循环, 接收日志写入 ring buffer
 *   cmd         — SOCK_STREAM accept 循环, 处理管理命令
 *   flusher     — condvar wait → flush ring buffer → file transport
 */

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

/* 共享状态 */
static elog_ring_buf_t s_daemon_rb;
static elog_prune_t s_daemon_prune;
elog_ring_buf_t* g_daemon_rb = &s_daemon_rb;
volatile bool g_daemon_running = false;

static pthread_t s_listener_tid;
static pthread_t s_cmd_tid;
static pthread_t s_flusher_tid;

/* 线程入口声明 (在其他文件定义) */
extern void* elogd_listener_thread(void* arg);
extern void* elogd_cmd_thread(void* arg);
extern void* elogd_flusher_thread(void* arg);

static void signal_handler(int sig) {
    (void)sig;
    g_daemon_running = false;
}

int elogd_run(void) {
    /* 信号处理 */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("elogd: starting...\n");

    /* 初始化 ring buffer */
    int ret = elog_ring_buf_init(&s_daemon_rb, ELOG_DAEMON_BUFFER_SIZE);
    if (ret != ELOG_OK) {
        fprintf(stderr, "elogd: ring buf init failed (%d)\n", ret);
        return ret;
    }

#if ELOG_PRUNE_ENABLE
    elog_prune_init(&s_daemon_prune, ELOG_PRUNE_THRESHOLD_PCT);
    elog_prune_load_rules(&s_daemon_prune, "~elogd ~crash");
    elog_ring_buf_set_prune(&s_daemon_rb, &s_daemon_prune);
#endif

    g_daemon_running = true;

    /* 启动线程 */
    pthread_create(&s_listener_tid, NULL, elogd_listener_thread, NULL);
    pthread_create(&s_cmd_tid, NULL, elogd_cmd_thread, NULL);
    pthread_create(&s_flusher_tid, NULL, elogd_flusher_thread, NULL);

    pthread_detach(s_listener_tid);
    pthread_detach(s_cmd_tid);
    pthread_detach(s_flusher_tid);

    printf("elogd: ready (buffer=%uKB)\n", (unsigned)(ELOG_DAEMON_BUFFER_SIZE / 1024));

    /* 主线程等待信号 */
    while (g_daemon_running) {
        pause();
    }

    /* 清理 */
    printf("elogd: shutting down...\n");

    /* 等待线程退出 (给 1 秒) */
    usleep(100000);

    elog_ring_buf_destroy(&s_daemon_rb);

    printf("elogd: stopped\n");
    return ELOG_OK;
}

void elogd_stop(void) {
    g_daemon_running = false;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    return elogd_run();
}
