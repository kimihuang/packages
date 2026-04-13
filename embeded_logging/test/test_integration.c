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

    for (int i = 0; i < 50; i++) {
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

static int send_log(const char* tag, const char* msg, uint8_t level) {
    elog_msg_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.log_id = ELOG_ID_MAIN;
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

/* 从 reader socket 读取所有匹配 tag 的条目 (tail=0, count=0) */
static int reader_read_by_tag(const char* tag_filter, log_entry_t* out, int max) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, s_reader_sock, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }

    elog_read_request_t req = { .version = ELOG_READ_PROTOCOL_VERSION };
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

    stop_elogd();

    printf("  Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
