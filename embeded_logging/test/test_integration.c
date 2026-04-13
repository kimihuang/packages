/**
 * @file test_integration.c
 * @brief 系统级端到端集成测试
 *
 * 测试流程: fork elogd → app 用 sendto 写日志 → reader socket 读取 → 验证
 *
 * 注意: 所有测试共享同一个 daemon 实例, 每个测试用唯一 tag 隔离数据.
 */

#include "elog_def.h"
#include "elogd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>

/* ===== 测试基础设施 ===== */

static pid_t s_elogd_pid = -1;
static char s_tmp_dir[128];
static char s_write_sock[128];
static char s_cmd_sock[128];
static char s_reader_sock[128];

static int g_pass = 0;
static int g_fail = 0;

#define T_ASSERT(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
        g_fail++; return; } \
} while(0)

#define T_OK(msg) do { printf("  PASS: %s\n", msg); g_pass++; } while(0)

static void make_tmp_paths(void) {
    snprintf(s_tmp_dir, sizeof(s_tmp_dir), "/tmp/elog_test_%d", getpid());
    snprintf(s_write_sock, sizeof(s_write_sock), "%s/w.sock", s_tmp_dir);
    snprintf(s_cmd_sock, sizeof(s_cmd_sock), "%s/c.sock", s_tmp_dir);
    snprintf(s_reader_sock, sizeof(s_reader_sock), "%s/r.sock", s_tmp_dir);
}

static void stop_elogd(void);

static int start_elogd(void) {
    make_tmp_paths();
    mkdir(s_tmp_dir, 0755);

    const char* elogd_path =
        "/home/lion/workdir/sourcecode/quantum_main/src/packages/embeded_logging/build/elogd";
    if (access(elogd_path, X_OK) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char wa[256], ca[256], ra[256];
        snprintf(wa, sizeof(wa), "-w%s", s_write_sock);
        snprintf(ca, sizeof(ca), "-c%s", s_cmd_sock);
        snprintf(ra, sizeof(ra), "-r%s", s_reader_sock);
        execl(elogd_path, "elogd", wa, ca, ra, NULL);
        _exit(1);
    }
    s_elogd_pid = pid;

    for (int i = 0; i < 100; i++) {
        usleep(20000);
        int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd < 0) continue;
        struct sockaddr_un addr = { .sun_family = AF_UNIX };
        strncpy(addr.sun_path, s_reader_sock, sizeof(addr.sun_path) - 1);
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(fd);
            return 0;
        }
        close(fd);
    }
    stop_elogd();
    return -1;
}

static void stop_elogd(void) {
    if (s_elogd_pid > 0) {
        kill(s_elogd_pid, SIGTERM);
        int st; waitpid(s_elogd_pid, &st, 0);
        s_elogd_pid = -1;
    }
    unlink(s_write_sock);
    unlink(s_cmd_sock);
    unlink(s_reader_sock);
    rmdir(s_tmp_dir);
}

/* ===== 直接构造 datagram 并发送 ===== */

static int send_log_ex(uint8_t log_id, const char* tag, const char* msg, uint8_t level);

static int send_log(const char* tag, const char* msg, uint8_t level) {
    return send_log_ex(ELOG_ID_MAIN, tag, msg, level);
}

/* 指定 log_id 发送日志 */
static int send_log_ex(uint8_t log_id, const char* tag, const char* msg, uint8_t level) {
    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = log_id;
    hdr.level = level;
    hdr.tag_len = (uint16_t)(tag ? strlen(tag) : 0);
    hdr.msg_len = (uint16_t)(msg ? strlen(msg) : 0);

    uint8_t buf[sizeof(elog_msg_header_t) + 512];
    memcpy(buf, &hdr, sizeof(hdr));
    if (hdr.tag_len > 0) memcpy(buf + sizeof(hdr), tag, hdr.tag_len);
    if (hdr.msg_len > 0) memcpy(buf + sizeof(hdr) + hdr.tag_len, msg, hdr.msg_len);

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, s_write_sock, sizeof(addr.sun_path) - 1);
    ssize_t sent = sendto(fd, buf, sizeof(hdr) + hdr.tag_len + hdr.msg_len,
                           0, (struct sockaddr*)&addr, sizeof(addr));
    close(fd);
    return (sent > 0) ? 0 : -1;
}

/* ===== Reader socket ===== */

typedef struct {
    elog_msg_header_t hdr;
    char tag[64];
    char msg[256];
} log_entry_t;

/* 带log_mask 读取 (mask=0 全部, mask=(1<<id) 只读指定 buffer) */
static int reader_read_by_tag_mask(const char* tag_filter, uint32_t log_mask,
                                    log_entry_t* out, int max);
static int reader_read_by_tag_mask(const char* tag_filter, uint32_t log_mask,
                                    log_entry_t* out, int max) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, s_reader_sock, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }

    elog_read_request_t req = { .version = ELOG_READ_PROTOCOL_VERSION, .log_mask = log_mask };
    if (send(fd, &req, sizeof(req), 0) < 0) { close(fd); return -1; }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    int n = 0;
    while (n < max) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0 || r < (ssize_t)sizeof(elog_msg_header_t)) break;

        elog_msg_header_t hdr;
        memcpy(&hdr, buf, sizeof(hdr));
        uint16_t tl = hdr.tag_len; if (tl > 63) tl = 63;
        char tag[64] = {0};
        memcpy(tag, buf + sizeof(hdr), tl);

        if (tag_filter && strcmp(tag, tag_filter) != 0) continue;

        memcpy(&out[n].hdr, &hdr, sizeof(hdr));
        strcpy(out[n].tag, tag);
        uint16_t ml = hdr.msg_len; if (ml > 255) ml = 255;
        memcpy(out[n].msg, buf + sizeof(hdr) + hdr.tag_len, ml);
        out[n].msg[ml] = '\0';
        n++;
    }
    close(fd);
    return n;
}

/* 便捷: 读全部 (log_mask=0) */
static int reader_read_by_tag(const char* tag_filter, log_entry_t* out, int max) {
    return reader_read_by_tag_mask(tag_filter, 0, out, max);
}

/* ===== 并发测试辅助 ===== */

/* 建立 cmd 连接并返回 fd */
static int cmd_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, s_cmd_sock, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

/* ===== 测试用例 ===== */

static void test_e2e_basic(void) {
    printf("  test_e2e_basic...\n");

    T_ASSERT(send_log("sensor", "temp=25.5", ELOG_LEVEL_INFO) == 0, "send 1");
    T_ASSERT(send_log("motor", "overload", ELOG_LEVEL_WARN) == 0, "send 2");
    T_ASSERT(send_log("crash", "segfault", ELOG_LEVEL_ERROR) == 0, "send 3");

    usleep(200000);

    log_entry_t e1[10], e2[10], e3[10];
    int n1 = reader_read_by_tag("sensor", e1, 10);
    int n2 = reader_read_by_tag("motor", e2, 10);
    int n3 = reader_read_by_tag("crash", e3, 10);

    T_ASSERT(n1 >= 1 && n2 >= 1 && n3 >= 1, "each tag found");
    T_ASSERT(strcmp(e1[0].msg, "temp=25.5") == 0, "sensor msg correct");
    T_ASSERT(strcmp(e2[0].msg, "overload") == 0, "motor msg correct");
    T_ASSERT(strcmp(e3[0].msg, "segfault") == 0, "crash msg correct");
    T_OK("e2e basic");
}

static void test_e2e_level_filter(void) {
    printf("  test_e2e_level_filter...\n");

    send_log("lf", "info_msg", ELOG_LEVEL_INFO);
    send_log("lf", "warn_msg", ELOG_LEVEL_WARN);
    send_log("lf", "error_msg", ELOG_LEVEL_ERROR);
    usleep(200000);

    /* 读全部, 手动检查 level */
    log_entry_t entries[10];
    int n = reader_read_by_tag("lf", entries, 10);
    T_ASSERT(n >= 3, "should receive >= 3 entries");

    int has_info = 0, has_warn = 0, has_error = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(entries[i].msg, "info_msg") == 0 && entries[i].hdr.level == ELOG_LEVEL_INFO) has_info = 1;
        if (strcmp(entries[i].msg, "warn_msg") == 0 && entries[i].hdr.level == ELOG_LEVEL_WARN) has_warn = 1;
        if (strcmp(entries[i].msg, "error_msg") == 0 && entries[i].hdr.level == ELOG_LEVEL_ERROR) has_error = 1;
    }
    T_ASSERT(has_info && has_warn && has_error, "all 3 levels found with correct level");
    T_OK("e2e level filter");
}

static void test_e2e_header_fields(void) {
    printf("  test_e2e_header_fields...\n");

    send_log("hdr", "test_msg", ELOG_LEVEL_ERROR);
    usleep(200000);

    log_entry_t entries[10];
    int n = reader_read_by_tag("hdr", entries, 10);
    T_ASSERT(n >= 1, "should receive >= 1");
    T_ASSERT(entries[0].hdr.level == ELOG_LEVEL_ERROR, "level is ERROR");
    T_ASSERT(entries[0].hdr.log_id == ELOG_ID_MAIN, "log_id is MAIN");
    T_ASSERT(strcmp(entries[0].tag, "hdr") == 0, "tag is hdr");
    T_ASSERT(strcmp(entries[0].msg, "test_msg") == 0, "msg is test_msg");
    T_OK("e2e header fields");
}

static void test_e2e_multi_entries(void) {
    printf("  test_e2e_multi_entries...\n");

    /* 连续写入多条, 验证顺序和完整性 */
    for (int i = 0; i < 10; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "entry_%d", i);
        send_log("multi", msg, ELOG_LEVEL_INFO);
    }
    usleep(300000);

    log_entry_t entries[20];
    int n = reader_read_by_tag("multi", entries, 20);
    T_ASSERT(n == 10, "should receive exactly 10");

    for (int i = 0; i < n; i++) {
        char expected[32];
        snprintf(expected, sizeof(expected), "entry_%d", i);
        T_ASSERT(strcmp(entries[i].msg, expected) == 0, "entry order correct");
    }
    T_OK("e2e multi entries");
}

static void test_e2e_cmd_stats(void) {
    printf("  test_e2e_cmd_stats...\n");

    send_log("stat", "a", ELOG_LEVEL_INFO);
    send_log("stat", "b", ELOG_LEVEL_WARN);
    usleep(200000);

    /* 发送 stats 命令, 验证不崩溃 */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    T_ASSERT(fd >= 0, "socket");
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, s_cmd_sock, sizeof(addr.sun_path) - 1);
    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    T_ASSERT(ret == 0, "cmd connect");
    write(fd, "stats\n", 6);

    char resp[1024] = {0};
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        ssize_t r = read(fd, resp + total, sizeof(resp) - 1 - total);
        if (r <= 0) break;
        total += (int)r;
    }
    close(fd);
    T_ASSERT(total > 0, "stats response not empty");
    T_ASSERT(strstr(resp, "capacity=") != NULL, "stats contains 'capacity='");
    T_ASSERT(strstr(resp, "count=") != NULL, "stats contains 'count='");
    T_OK("e2e cmd stats");
}

static void test_e2e_multi_buffer(void) {
    printf("  test_e2e_multi_buffer...\n");

    /* 向不同 buffer 写入, 用唯一 tag */
    send_log_ex(ELOG_ID_MAIN, "mb_main", "hello_main", ELOG_LEVEL_INFO);
    send_log_ex(ELOG_ID_RADIO, "mb_radio", "hello_radio", ELOG_LEVEL_INFO);
    send_log_ex(ELOG_ID_SYSTEM, "mb_system", "hello_system", ELOG_LEVEL_INFO);
    usleep(200000);

    /* 各自读到自己的 */
    log_entry_t e_main[10], e_radio[10], e_system[10];
    int n1 = reader_read_by_tag("mb_main", e_main, 10);
    int n2 = reader_read_by_tag("mb_radio", e_radio, 10);
    int n3 = reader_read_by_tag("mb_system", e_system, 10);

    T_ASSERT(n1 >= 1, "main entries found");
    T_ASSERT(n2 >= 1, "radio entries found");
    T_ASSERT(n3 >= 1, "system entries found");
    T_ASSERT(e_main[0].hdr.log_id == ELOG_ID_MAIN, "main log_id correct");
    T_ASSERT(e_radio[0].hdr.log_id == ELOG_ID_RADIO, "radio log_id correct");
    T_ASSERT(e_system[0].hdr.log_id == ELOG_ID_SYSTEM, "system log_id correct");
    T_OK("e2e multi buffer");
}

static void test_e2e_log_mask(void) {
    printf("  test_e2e_log_mask...\n");

    /* 向 3 个 buffer 写入 */
    send_log_ex(ELOG_ID_MAIN, "lm_main", "m", ELOG_LEVEL_INFO);
    send_log_ex(ELOG_ID_RADIO, "lm_radio", "r", ELOG_LEVEL_INFO);
    send_log_ex(ELOG_ID_CRASH, "lm_crash", "c", ELOG_LEVEL_INFO);
    usleep(200000);

    /* 只订阅 RADIO, 不应收到 MAIN 或 CRASH */
    log_entry_t entries[10];
    int n = reader_read_by_tag_mask(NULL, (1 << ELOG_ID_RADIO), entries, 10);
    T_ASSERT(n >= 1, "radio entries received");
    for (int i = 0; i < n; i++) {
        T_ASSERT(entries[i].hdr.log_id == ELOG_ID_RADIO, "all are RADIO");
    }
    T_OK("e2e log mask");
}

static void test_e2e_buffer_stats(void) {
    printf("  test_e2e_buffer_stats...\n");

    /* 向 3 个 buffer 各写一条 */
    send_log_ex(ELOG_ID_MAIN, "bs_main", "x", ELOG_LEVEL_INFO);
    send_log_ex(ELOG_ID_RADIO, "bs_radio", "y", ELOG_LEVEL_INFO);
    usleep(200000);

    /* stats 命令应返回多行, 每行一个 buffer */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    T_ASSERT(fd >= 0, "socket");
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, s_cmd_sock, sizeof(addr.sun_path) - 1);
    int ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    T_ASSERT(ret == 0, "connect");

    write(fd, "stats\n", 6);
    char resp[2048] = {0};
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        ssize_t r = read(fd, resp + total, sizeof(resp) - 1 - total);
        if (r <= 0) break;
        total += (int)r;
    }
    close(fd);

    T_ASSERT(strstr(resp, "main") != NULL, "stats has 'main'");
    T_ASSERT(strstr(resp, "radio") != NULL, "stats has 'radio'");
    T_ASSERT(strstr(resp, "events") != NULL, "stats has 'events'");
    T_ASSERT(strstr(resp, "system") != NULL, "stats has 'system'");
    T_ASSERT(strstr(resp, "crash") != NULL, "stats has 'crash'");
    T_ASSERT(strstr(resp, "kernel") != NULL, "stats has 'kernel'");

    /* 指定 buffer 查询 */
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    write(fd, "stats radio\n", 12);
    memset(resp, 0, sizeof(resp));
    total = 0;
    while (total < (int)sizeof(resp) - 1) {
        ssize_t r = read(fd, resp + total, sizeof(resp) - 1 - total);
        if (r <= 0) break;
        total += (int)r;
    }
    close(fd);

    T_ASSERT(strstr(resp, "radio") != NULL, "stats radio has 'radio'");
    T_ASSERT(strstr(resp, "main") == NULL, "stats radio has no 'main'");
    T_OK("e2e buffer stats");
}

/* ===== 并发测试 ===== */

/* 多进程并发写: fork N 个子进程, 各自写 50 条, 读端验证总数 */
static void test_concurrent_writers(void) {
    printf("  test_concurrent_writers...\n");

    const int NPROC = 4;
    const int PER_PROC = 50;
    pid_t pids[NPROC];

    for (int p = 0; p < NPROC; p++) {
        pid_t pid = fork();
        if (pid < 0) { T_ASSERT(0, "fork"); return; }
        if (pid == 0) {
            /* 子进程: 快速写入 */
            for (int i = 0; i < PER_PROC; i++) {
                char msg[32];
                snprintf(msg, sizeof(msg), "p%d_%d", p, i);
                send_log("cw", msg, ELOG_LEVEL_INFO);
            }
            _exit(0);
        }
        pids[p] = pid;
    }

    /* 等待所有子进程完成 */
    for (int i = 0; i < NPROC; i++) waitpid(pids[i], NULL, 0);
    usleep(300000);

    /* 读端验证: 应收到 NPROC * PER_PROC 条 */
    log_entry_t entries[300];
    int n = reader_read_by_tag("cw", entries, 300);
    T_ASSERT(n >= NPROC * PER_PROC, "received enough entries");
    T_OK("concurrent writers");
}

/* 多 reader 并发: 同时开 2 个 reader 各订阅不同 buffer */
static void test_concurrent_readers(void) {
    printf("  test_concurrent_readers...\n");

    /* 写入不同 buffer */
    for (int i = 0; i < 20; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "r%d", i);
        send_log_ex(ELOG_ID_MAIN, "cr", msg, ELOG_LEVEL_INFO);
        snprintf(msg, sizeof(msg), "s%d", i);
        send_log_ex(ELOG_ID_SYSTEM, "cr_sys", msg, ELOG_LEVEL_INFO);
    }
    usleep(200000);

    /* fork 2 个 reader 子进程 */
    int pipefd[2];
    pipe(pipefd);

    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(pipefd[0]);
        log_entry_t entries[30];
        int n = reader_read_by_tag_mask("cr", (1 << ELOG_ID_MAIN), entries, 30);
        write(pipefd[1], &n, sizeof(n));
        _exit(n >= 20 ? 0 : 1);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(pipefd[0]);
        log_entry_t entries[30];
        int n = reader_read_by_tag_mask("cr_sys", (1 << ELOG_ID_SYSTEM), entries, 30);
        write(pipefd[1], &n, sizeof(n));
        _exit(n >= 20 ? 0 : 1);
    }

    close(pipefd[1]);
    int n1 = 0, n2 = 0;
    read(pipefd[0], &n1, sizeof(n1));
    read(pipefd[0], &n2, sizeof(n2));
    close(pipefd[0]);

    int st1, st2;
    waitpid(pid1, &st1, 0);
    waitpid(pid2, &st2, 0);

    T_ASSERT(n1 >= 20, "reader1 got main entries");
    T_ASSERT(n2 >= 20, "reader2 got system entries");
    T_OK("concurrent readers");
}

/* 写 + 读并发: fork writer + reader 子进程同时操作, 验证无崩溃且数据完整 */
static void test_concurrent_write_read(void) {
    printf("  test_concurrent_write_read...\n");

    const int TOTAL = 100;
    int r_pipe[2], w_pipe[2];
    pipe(r_pipe);   /* reader → parent: count */
    pipe(w_pipe);   /* parent → reader: start signal */

    pid_t reader_pid = fork();
    if (reader_pid == 0) {
        close(r_pipe[0]); close(w_pipe[1]);
        /* 等待 parent 信号 */
        char sig = 0;
        read(w_pipe[0], &sig, 1);
        /* 用 tail=0 从头读, 通过 tag 过滤 */
        log_entry_t entries[200];
        int n = reader_read_by_tag("cwr", entries, 200);
        write(r_pipe[1], &n, sizeof(n));
        _exit(0);
    }

    close(r_pipe[1]); close(w_pipe[0]);

    /* writer 先写, reader 同时连接并读 */
    for (int i = 0; i < TOTAL; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "wr%d", i);
        send_log("cwr", msg, ELOG_LEVEL_INFO);
    }

    /* 通知 reader 开始读 */
    char sig = 1;
    write(w_pipe[1], &sig, 1);

    int reader_count = 0;
    read(r_pipe[0], &reader_count, sizeof(reader_count));
    close(r_pipe[0]); close(w_pipe[1]);
    waitpid(reader_pid, NULL, 0);

    T_ASSERT(reader_count >= TOTAL, "reader got all entries");
    T_OK("concurrent write+read");
}

/* 写 + cmd 并发: 一边写, 一边发 stats, 验证不崩溃 */
static void test_concurrent_write_cmd(void) {
    printf("  test_concurrent_write_cmd...\n");

    const int TOTAL = 200;

    /* fork: 子进程疯狂发 stats */
    pid_t cmd_pid = fork();
    if (cmd_pid == 0) {
        for (int i = 0; i < 50; i++) {
            int fd = cmd_connect();
            if (fd >= 0) {
                write(fd, "stats\n", 6);
                char buf[2048];
                while (read(fd, buf, sizeof(buf)) > 0) {}
                close(fd);
            }
            usleep(1000);
        }
        _exit(0);
    }

    /* 父进程: 同时写日志 */
    for (int i = 0; i < TOTAL; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "wc%d", i);
        send_log("cwc", msg, ELOG_LEVEL_INFO);
    }

    int st;
    waitpid(cmd_pid, &st, 0);

    T_ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 0, "cmd process exited normally");

    /* 验证: 数据应完整 */
    usleep(200000);
    log_entry_t entries[300];
    int n = reader_read_by_tag("cwc", entries, 300);
    T_ASSERT(n >= TOTAL, "all writes survived");
    T_OK("concurrent write+cmd");
}

/* 多 buffer 并发写: 多进程同时写不同 buffer, 各自读取无交叉 */
static void test_concurrent_multi_buffer(void) {
    printf("  test_concurrent_multi_buffer...\n");

    const int PER_BUF = 30;

    /* 3 个子进程各写一个 buffer */
    pid_t p_main = fork();
    if (p_main == 0) {
        for (int i = 0; i < PER_BUF; i++) {
            char msg[32]; snprintf(msg, sizeof(msg), "m%d", i);
            send_log_ex(ELOG_ID_MAIN, "cmb_m", msg, ELOG_LEVEL_INFO);
        }
        _exit(0);
    }

    pid_t p_radio = fork();
    if (p_radio == 0) {
        for (int i = 0; i < PER_BUF; i++) {
            char msg[32]; snprintf(msg, sizeof(msg), "r%d", i);
            send_log_ex(ELOG_ID_RADIO, "cmb_r", msg, ELOG_LEVEL_INFO);
        }
        _exit(0);
    }

    pid_t p_crash = fork();
    if (p_crash == 0) {
        for (int i = 0; i < PER_BUF; i++) {
            char msg[32]; snprintf(msg, sizeof(msg), "c%d", i);
            send_log_ex(ELOG_ID_CRASH, "cmb_c", msg, ELOG_LEVEL_ERROR);
        }
        _exit(0);
    }

    waitpid(p_main, NULL, 0);
    waitpid(p_radio, NULL, 0);
    waitpid(p_crash, NULL, 0);
    usleep(300000);

    /* 各自读, 验证无交叉 */
    log_entry_t e_main[50], e_radio[50], e_crash[50];
    int n1 = reader_read_by_tag_mask("cmb_m", (1 << ELOG_ID_MAIN), e_main, 50);
    int n2 = reader_read_by_tag_mask("cmb_r", (1 << ELOG_ID_RADIO), e_radio, 50);
    int n3 = reader_read_by_tag_mask("cmb_c", (1 << ELOG_ID_CRASH), e_crash, 50);

    T_ASSERT(n1 >= PER_BUF, "main got its entries");
    T_ASSERT(n2 >= PER_BUF, "radio got its entries");
    T_ASSERT(n3 >= PER_BUF, "crash got its entries");

    /* 验证隔离: 所有 main 条目 level=INFO, 所有 crash 条目 level=ERROR */
    int main_ok = 1, crash_ok = 1;
    for (int i = 0; i < n1 && main_ok; i++)
        if (e_main[i].hdr.log_id != ELOG_ID_MAIN) main_ok = 0;
    for (int i = 0; i < n3 && crash_ok; i++)
        if (e_crash[i].hdr.log_id != ELOG_ID_CRASH) crash_ok = 0;
    T_ASSERT(main_ok, "main entries isolated");
    T_ASSERT(crash_ok, "crash entries isolated");
    T_OK("concurrent multi-buffer");
}

/* ===== 二进制事件 E2E ===== */

static void test_e2e_binary_event(void) {
    printf("  test_e2e_binary_event...\n");

    /* 手动构造一个带嵌入 NUL 的二进制事件 datagram */
    uint8_t event_data[32];
    uint32_t event_id = 9999;
    memcpy(event_data, &event_id, 4);
    /* LIST header */
    event_data[4] = ELOG_EVENT_TYPE_LIST;
    event_data[5] = 2;
    /* INT32 element */
    event_data[6] = ELOG_EVENT_TYPE_INT32;
    int32_t ival = 42;
    memcpy(event_data + 7, &ival, 4);
    /* STRING element with embedded NUL */
    event_data[11] = ELOG_EVENT_TYPE_STRING;
    uint32_t slen = 5;
    memcpy(event_data + 12, &slen, 4);
    memcpy(event_data + 16, "h\0i!", 5);  /* contains NUL byte */
    size_t data_len = 21;

    /* 构造 wire datagram, log_id=ELOG_ID_EVENTS */
    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = ELOG_ID_EVENTS;
    hdr.level = ELOG_LEVEL_INFO;
    const char* tag = "bintest";
    hdr.tag_len = (uint16_t)strlen(tag);
    hdr.msg_len = (uint16_t)data_len;

    uint8_t buf[sizeof(elog_msg_header_t) + 256];
    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), tag, hdr.tag_len);
    memcpy(buf + sizeof(hdr) + hdr.tag_len, event_data, data_len);
    size_t total = sizeof(hdr) + hdr.tag_len + hdr.msg_len;

    /* 发送 */
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    T_ASSERT(fd >= 0, "socket");
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, s_write_sock, sizeof(addr.sun_path) - 1);
    ssize_t sent = sendto(fd, buf, total, 0, (struct sockaddr*)&addr, sizeof(addr));
    close(fd);
    T_ASSERT(sent > 0, "send binary event");

    usleep(200000);

    /* 从 EVENTS buffer 读回 */
    log_entry_t entries[10];
    int n = reader_read_by_tag_mask("bintest", (1 << ELOG_ID_EVENTS), entries, 10);
    T_ASSERT(n >= 1, "received binary event");
    T_ASSERT(entries[0].hdr.log_id == ELOG_ID_EVENTS, "log_id is EVENTS");
    T_ASSERT(entries[0].hdr.msg_len == (uint16_t)data_len, "msg_len preserved");

    /* 验证 event_id */
    uint32_t recv_id;
    memcpy(&recv_id, entries[0].msg, 4);
    T_ASSERT(recv_id == 9999, "event_id matches");

    /* 验证嵌入 NUL 完整保留 */
    T_ASSERT(entries[0].msg[17] == '\0', "embedded NUL preserved");
    T_ASSERT(entries[0].msg[18] == 'i', "data after NUL preserved");

    T_OK("e2e binary event");
}

/* ===== main ===== */

int main(void) {
    printf("elog_integration_test:\n");

    if (start_elogd() != 0) {
        fprintf(stderr, "SKIP: elogd failed to start\n");
        return 0;
    }
    printf("  elogd started (pid=%d)\n", (int)s_elogd_pid);

    test_e2e_basic();
    test_e2e_level_filter();
    test_e2e_header_fields();
    test_e2e_multi_entries();
    test_e2e_cmd_stats();
    test_e2e_multi_buffer();
    test_e2e_log_mask();
    test_e2e_buffer_stats();
    test_concurrent_writers();
    test_concurrent_readers();
    test_concurrent_write_read();
    test_concurrent_write_cmd();
    test_concurrent_multi_buffer();
    test_e2e_binary_event();

    stop_elogd();

    printf("  Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
