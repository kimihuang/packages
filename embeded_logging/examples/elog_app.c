/**
 * @file elog_app.c
 * @brief elog 示例应用 — 连接已运行的 elogd 守护进程，演示 ELOG_* API 用法
 *
 * 前提: elogd 已在 /usr/sbin/elogd 启动（使用默认 socket 路径）
 *
 * 用法:
 *   elog_app              # 每秒输出各 buffer/级别日志，Ctrl+C 退出
 *   elog_app -n <count>   # 输出指定条数后退出（默认无限循环）
 */

#include "elog.h"
#include "elog_def.h"
#include "elog_event.h"
#include "elogd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

/* ===== 自定义 logger: 发送到 elogd 守护进程 ===== */

static void daemon_logger(const elog_msg_header_t* hdr,
                           const char* tag, const char* msg) {
    elogd_client_send(hdr, tag, msg);
}

/* ===== 初始化 ===== */

static int app_init(void) {
    if (elog_init() != ELOG_OK) {
        fprintf(stderr, "elog_app: elog_init failed\n");
        return -1;
    }

    /* 使用默认 socket 路径连接已运行的 elogd */
    g_daemon_write_sock = ELOG_DAEMON_SOCK_PATH;
    g_daemon_cmd_sock    = ELOG_DAEMON_CMD_SOCK;
    g_daemon_reader_sock = ELOG_DAEMON_READER_SOCK;

    if (elogd_client_init() != ELOG_OK) {
        fprintf(stderr, "elog_app: elogd_client_init failed (is elogd running?)\n");
        elog_deinit();
        return -1;
    }

    /* 替换默认 stdout logger，日志发送到 elogd */
    elog_set_logger(daemon_logger);

    return 0;
}

/* ===== 日志演示 ===== */

static void demo_main_buffer(int seq) {
    ELOG_I("app", "=== round %d ===", seq);
    ELOG_V("app", "verbose detail: temp=%.1f", 25.3);
    ELOG_D("app", "debug: buffer usage check");
    ELOG_I("app", "info: system started");
    ELOG_W("app", "warn: memory usage high (%d%%)", 85);
    ELOG_E("app", "error: connection timeout");
    ELOG_F("app", "fatal: unrecoverable error!");
}

static void demo_radio_buffer(void) {
    ELOG_RADIO_V("radio", "signal scan start");
    ELOG_RADIO_I("radio", "connected to AP: %s", "MyWiFi");
    ELOG_RADIO_W("radio", "signal weak: -%ddBm", 78);
    ELOG_RADIO_E("radio", "disconnected unexpectedly");
}

static void demo_system_buffer(void) {
    ELOG_SYSTEM_I("system", "cpu load: %.1f%%", 45.2);
    ELOG_SYSTEM_W("system", "disk usage: %d%%", 92);
    ELOG_SYSTEM_E("system", "out of memory, pid=%d killed", 1234);
}

static void demo_events_buffer(void) {
    /* 使用 elog_event API 编码 TLV 事件 */
    elog_event_ctx_t* evt = elog_event_create(1001);
    if (evt) {
        elog_event_add_int32(evt, 42);
        elog_event_add_string(evt, "hello");
        elog_event_add_float(evt, 3.14f);
        elog_event_submit(evt, "sensor");
    }
}

static void demo_crash_buffer(void) {
    ELOG_CRASH_E("crash", "segfault at addr=0x%p", (void*)0xdeadbeef);
}

static void demo_kernel_buffer(void) {
    ELOG_KERNEL_W("kernel", "oops in %s:%d", "driver.c", 256);
}

static void demo_write_ex(void) {
    /* elog_write_ex: 向任意 buffer 写入 */
    elog_write_ex(ELOG_ID_MAIN, ELOG_LEVEL_INFO, "write_ex", "pid=%d seq=%d",
                  (int)elog_port_getpid(), 1);
}

/* ===== 主函数 ===== */

static void usage(const char* prog) {
    printf("Usage: %s [options]\n"
           "Options:\n"
           "  -n COUNT    Output COUNT rounds then exit (default: infinite)\n"
           "  -h          Show this help\n",
           prog);
}

int main(int argc, char* argv[]) {
    int count = 0;  /* 0 = infinite */
    int opt;
    while ((opt = getopt(argc, argv, "n:h")) != -1) {
        switch (opt) {
        case 'n': count = atoi(optarg); break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (app_init() != 0) return 1;

    fprintf(stdout, "elog_app: started (pid=%d), press Ctrl+C to stop\n",
            (int)elog_port_getpid());

    int seq = 0;
    while (g_running) {
        demo_main_buffer(seq);
        demo_radio_buffer();
        demo_system_buffer();
        demo_events_buffer();
        demo_crash_buffer();
        demo_kernel_buffer();
        demo_write_ex();

        seq++;
        if (count > 0 && seq >= count) break;
        sleep(1);
    }

    elog_deinit();
    fprintf(stdout, "elog_app: stopped after %d rounds\n", seq);
    return 0;
}
