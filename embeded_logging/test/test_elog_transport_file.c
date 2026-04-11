/**
 * @file test_elog_transport_file.c
 * @brief FileTransport 单元测试
 */

#define _DEFAULT_SOURCE  /* mkdtemp, stat */

#include "elog_transport_file.h"
#include "elog_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_LOG_DIR    "/tmp/elog_test_XXXXXX"
#define TEST_LOG_FILE   "test.log"

static char g_test_dir[ELOG_FILE_PATH_MAX];
static char g_log_path[ELOG_FILE_PATH_MAX];

static void setup(void) {
    /* 创建临时目录 */
    strncpy(g_test_dir, TEST_LOG_DIR, sizeof(g_test_dir) - 1);
    mkdtemp(g_test_dir);
    snprintf(g_log_path, sizeof(g_log_path), "%s/%s", g_test_dir, TEST_LOG_FILE);
}

static void teardown(void) {
    /* 递归删除临时目录 */
    char cmd[ELOG_FILE_PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_dir);
    (void)system(cmd);
}

/* 读取整个文件到 buffer */
static int read_file(const char* path, char* buf, size_t len) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, len - 1, f);
    fclose(f);
    buf[n] = '\0';
    return (int)n;
}

/* 统计匹配 .N 后缀的文件数量 */
static int count_rotated_files(void) {
    int count = 0;
    char path[ELOG_FILE_PATH_MAX];
    for (int i = 1; i <= 10; i++) {
        snprintf(path, sizeof(path), "%s.%d", g_log_path, i);
        if (access(path, F_OK) == 0) count++;
    }
    return count;
}

static void test_file_init_destroy(void) {
    printf("  test_file_init_destroy...\n");

    elog_file_transport_t ft;
    int ret = elog_file_transport_init(&ft, g_log_path, 4096, 3);
    assert(ret == ELOG_OK);
    assert(strcmp(ft.filepath, g_log_path) == 0);
    assert(ft.max_file_size == 4096);
    assert(ft.max_files == 3);
    assert(ft.fd == -1);
    assert(ft.is_open_ == false);
    assert(ft.base.is_open(&ft.base) == false);
    assert(strcmp(ft.base.name(&ft.base), "file") == 0);

    /* 默认参数 */
    elog_file_transport_t ft2;
    ret = elog_file_transport_init(&ft2, NULL, 0, 0);
    assert(ret == ELOG_OK);
    assert(strcmp(ft2.filepath, ELOG_FILE_DEFAULT_PATH) == 0);
    assert(ft2.max_file_size == ELOG_FILE_DEFAULT_SIZE);
    assert(ft2.max_files == ELOG_FILE_MAX_FILES);

    elog_file_transport_deinit(&ft);
    elog_file_transport_deinit(&ft2);

    /* NULL 不崩溃 */
    elog_file_transport_deinit(NULL);
    elog_file_transport_cleanup(NULL);
}

static void test_file_write_and_read(void) {
    printf("  test_file_write_and_read...\n");

    elog_file_transport_t ft;
    elog_file_transport_init(&ft, g_log_path, 4096, 3);
    ft.base.open(&ft.base);
    assert(ft.base.is_open(&ft.base) == true);

    const char* msg = "hello file transport\n";
    int ret = ft.base.write(&ft.base, (const uint8_t*)msg, strlen(msg));
    assert(ret == (int)strlen(msg));
    assert(ft.file_size == strlen(msg));

    const char* msg2 = "second line\n";
    ret = ft.base.write(&ft.base, (const uint8_t*)msg2, strlen(msg2));
    assert(ret == (int)strlen(msg2));

    ft.base.close(&ft.base);

    /* 读取文件验证内容 */
    char buf[1024];
    int n = read_file(g_log_path, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "hello file transport") != NULL);
    assert(strstr(buf, "second line") != NULL);
}

static void test_file_rotation(void) {
    printf("  test_file_rotation...\n");

    /* 小文件 64B, 保留 3 个文件 */
    elog_file_transport_t ft;
    elog_file_transport_init(&ft, g_log_path, 64, 3);
    ft.base.open(&ft.base);

    /* 写入超过 64B 的数据触发轮转 */
    char data[32];
    memset(data, 'X', sizeof(data) - 1);
    data[sizeof(data) - 1] = '\n';

    for (int i = 0; i < 10; i++) {
        ft.base.write(&ft.base, (const uint8_t*)data, strlen(data));
    }

    ft.base.close(&ft.base);

    /* 应有轮转文件 */
    int rotated = count_rotated_files();
    printf("    (rotated files: %d)\n", rotated);
    assert(rotated >= 1);

    /* 主文件应存在 */
    assert(access(g_log_path, F_OK) == 0);

    /* 主文件大小应 <= 64 + 单条大小 */
    struct stat st;
    assert(stat(g_log_path, &st) == 0);
    assert((size_t)st.st_size <= 64 + 32);
}

static void test_file_flush(void) {
    printf("  test_file_flush...\n");

    elog_file_transport_t ft;
    elog_file_transport_init(&ft, g_log_path, 4096, 3);
    ft.base.open(&ft.base);

    const char* msg = "flush test data\n";
    ft.base.write(&ft.base, (const uint8_t*)msg, strlen(msg));

    int ret = ft.base.flush(&ft.base);
    assert(ret == ELOG_OK);

    ft.base.close(&ft.base);

    /* 读取验证 */
    char buf[256];
    int n = read_file(g_log_path, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "flush test data") != NULL);
}

static void test_file_not_open(void) {
    printf("  test_file_not_open...\n");

    elog_file_transport_t ft;
    elog_file_transport_init(&ft, g_log_path, 4096, 3);

    /* 未 open, write 应返回错误 */
    int ret = ft.base.write(&ft.base, (const uint8_t*)"test", 4);
    assert(ret == ELOG_ERR_PARAM);

    /* flush 未 open 也应返回错误 */
    ret = ft.base.flush(&ft.base);
    assert(ret == ELOG_ERR_PARAM);

    /* dispatch 跳过未 open 的 transport */
    elog_transport_registry_t reg;
    elog_transport_registry_init(&reg);
    elog_transport_register(&reg, &ft.base);
    elog_transport_dispatch(&reg, (const uint8_t*)"test", 4);
    /* 不崩溃即通过 */
    elog_transport_registry_destroy(&reg);
}

static void test_file_cleanup(void) {
    printf("  test_file_cleanup...\n");

    elog_file_transport_t ft;
    elog_file_transport_init(&ft, g_log_path, 64, 3);
    ft.base.open(&ft.base);

    /* 写入触发轮转 */
    char data[32];
    memset(data, 'Y', sizeof(data) - 1);
    data[sizeof(data) - 1] = '\n';
    for (int i = 0; i < 10; i++) {
        ft.base.write(&ft.base, (const uint8_t*)data, strlen(data));
    }
    ft.base.close(&ft.base);

    /* 确认有轮转文件 */
    assert(count_rotated_files() >= 1);
    assert(access(g_log_path, F_OK) == 0);

    /* cleanup 删除所有文件 */
    elog_file_transport_cleanup(&ft);
    assert(access(g_log_path, F_OK) != 0);
    assert(count_rotated_files() == 0);
}

static void test_file_registry_integration(void) {
    printf("  test_file_registry_integration...\n");

    elog_file_transport_t ft;
    elog_file_transport_init(&ft, g_log_path, 4096, 3);
    ft.base.open(&ft.base);

    elog_transport_registry_t reg;
    elog_transport_registry_init(&reg);
    elog_transport_register(&reg, &ft.base);

    /* 通过 registry 分发 */
    const char* msg = "dispatched to file\n";
    elog_transport_dispatch(&reg, (const uint8_t*)msg, strlen(msg));

    elog_transport_flush_all(&reg);

    elog_transport_unregister(&reg, &ft.base);
    elog_transport_registry_destroy(&reg);
    ft.base.close(&ft.base);

    /* 验证文件内容 */
    char buf[256];
    int n = read_file(g_log_path, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, "dispatched to file") != NULL);
}

int test_elog_transport_file(void) {
    printf("test_elog_transport_file:\n");

    setup();

    test_file_init_destroy();
    test_file_write_and_read();
    test_file_rotation();
    test_file_flush();
    test_file_not_open();
    test_file_cleanup();
    test_file_registry_integration();

    teardown();

    return 0;
}
