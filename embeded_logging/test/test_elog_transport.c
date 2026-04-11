/**
 * @file test_elog_transport.c
 * @brief Transport + Registry 单元测试
 */

#include "elog_transport.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

/* Mock Transport: 记录 write 调用 */
typedef struct {
    elog_transport_t base;
    int  write_count;
    int  open_count;
    int  close_count;
    int  flush_count;
    bool is_open_;
    uint8_t last_data[256];
    size_t last_len;
} mock_transport_t;

static int mock_open(elog_transport_t* self) {
    mock_transport_t* t = (mock_transport_t*)self;
    t->is_open_ = true;
    t->open_count++;
    return ELOG_OK;
}

static void mock_close(elog_transport_t* self) {
    mock_transport_t* t = (mock_transport_t*)self;
    t->is_open_ = false;
    t->close_count++;
}

static int mock_write(elog_transport_t* self, const uint8_t* data, size_t len) {
    mock_transport_t* t = (mock_transport_t*)self;
    t->write_count++;
    size_t copy = len < sizeof(t->last_data) ? len : sizeof(t->last_data);
    memcpy(t->last_data, data, copy);
    t->last_len = copy;
    return (int)len;
}

static int mock_flush(elog_transport_t* self) {
    mock_transport_t* t = (mock_transport_t*)self;
    t->flush_count++;
    return ELOG_OK;
}

static bool mock_is_open(elog_transport_t* self) {
    mock_transport_t* t = (mock_transport_t*)self;
    return t->is_open_;
}

static const char* mock_name(elog_transport_t* self) {
    (void)self;
    return "mock";
}

static void mock_init(mock_transport_t* t) {
    memset(t, 0, sizeof(*t));
    t->base.open    = mock_open;
    t->base.close   = mock_close;
    t->base.write   = mock_write;
    t->base.flush   = mock_flush;
    t->base.is_open = mock_is_open;
    t->base.name    = mock_name;
}

static void test_registry_init(void) {
    printf("  test_registry_init...\n");

    elog_transport_registry_t r;
    elog_transport_registry_init(&r);
    assert(r.initialized == true);
    assert(r.count == 0);

    elog_transport_registry_destroy(&r);
}

static void test_registry_register_unregister(void) {
    printf("  test_registry_register_unregister...\n");

    elog_transport_registry_t r;
    elog_transport_registry_init(&r);

    mock_transport_t mock1, mock2;
    mock_init(&mock1);
    mock_init(&mock2);

    /* 注册 */
    int ret = elog_transport_register(&r, &mock1.base);
    assert(ret == ELOG_OK);
    assert(r.count == 1);

    ret = elog_transport_register(&r, &mock2.base);
    assert(ret == ELOG_OK);
    assert(r.count == 2);

    /* 重复注册同一 transport */
    ret = elog_transport_register(&r, &mock1.base);
    assert(ret == ELOG_OK);
    assert(r.count == 3);

    /* 注销 */
    ret = elog_transport_unregister(&r, &mock1.base);
    assert(ret == ELOG_OK);
    assert(r.count == 2);  /* 移除第一个 mock1, 还剩 mock2 + mock1 */

    /* 再次注销 mock1 (第二个实例) */
    ret = elog_transport_unregister(&r, &mock1.base);
    assert(ret == ELOG_OK);
    assert(r.count == 1);  /* 只剩 mock2 */

    /* 注销不存在的 (mock1 已全部移除) */
    ret = elog_transport_unregister(&r, &mock1.base);
    assert(ret == ELOG_ERR_PARAM);
    assert(r.count == 1);

    elog_transport_registry_destroy(&r);
}

static void test_registry_dispatch(void) {
    printf("  test_registry_dispatch...\n");

    elog_transport_registry_t r;
    elog_transport_registry_init(&r);

    mock_transport_t mock1, mock2;
    mock_init(&mock1);
    mock_init(&mock2);
    mock1.base.open(&mock1.base);
    mock2.base.open(&mock2.base);

    elog_transport_register(&r, &mock1.base);
    elog_transport_register(&r, &mock2.base);

    /* 分发 */
    const char* msg = "hello transport";
    elog_transport_dispatch(&r, (const uint8_t*)msg, strlen(msg));

    assert(mock1.write_count == 1);
    assert(mock2.write_count == 1);
    assert(mock1.last_len == strlen(msg));
    assert(memcmp(mock1.last_data, msg, strlen(msg)) == 0);

    /* 未 open 的 transport 不应收到数据 */
    mock2.base.close(&mock2.base);
    elog_transport_dispatch(&r, (const uint8_t*)msg, strlen(msg));
    assert(mock1.write_count == 2);
    assert(mock2.write_count == 1);  /* 不变 */

    elog_transport_registry_destroy(&r);
}

static void test_registry_flush_all(void) {
    printf("  test_registry_flush_all...\n");

    elog_transport_registry_t r;
    elog_transport_registry_init(&r);

    mock_transport_t mock1;
    mock_init(&mock1);
    mock1.base.open(&mock1.base);
    elog_transport_register(&r, &mock1.base);

    elog_transport_flush_all(&r);
    assert(mock1.flush_count == 1);

    elog_transport_flush_all(&r);
    assert(mock1.flush_count == 2);

    elog_transport_registry_destroy(&r);
}

static void test_registry_max(void) {
    printf("  test_registry_max...\n");

    elog_transport_registry_t r;
    elog_transport_registry_init(&r);

    mock_transport_t mocks[ELOG_MAX_TRANSPORTS + 2];
    for (int i = 0; i < ELOG_MAX_TRANSPORTS + 2; i++) {
        mock_init(&mocks[i]);
    }

    int ok_count = 0;
    for (int i = 0; i < ELOG_MAX_TRANSPORTS + 2; i++) {
        int ret = elog_transport_register(&r, &mocks[i].base);
        if (ret == ELOG_OK) ok_count++;
    }
    assert(ok_count == ELOG_MAX_TRANSPORTS);

    /* 额外注册应失败 */
    int ret = elog_transport_register(&r, &mocks[ELOG_MAX_TRANSPORTS + 1].base);
    assert(ret == ELOG_ERR_FULL);

    elog_transport_registry_destroy(&r);
}

static void test_stdout_transport(void) {
    printf("  test_stdout_transport...\n");

    elog_stdout_transport_t t;
    int ret = elog_stdout_transport_init(&t);
    assert(ret == ELOG_OK);
    assert(t.is_open_ == false);

    ret = t.base.open(&t.base);
    assert(ret == ELOG_OK);
    assert(t.base.is_open(&t.base) == true);
    assert(strcmp(t.base.name(&t.base), "stdout") == 0);

    /* 写入 */
    const char* msg = "test stdout transport\n";
    ret = t.base.write(&t.base, (const uint8_t*)msg, strlen(msg));
    assert(ret == (int)strlen(msg));

    ret = t.base.flush(&t.base);
    assert(ret == ELOG_OK);

    t.base.close(&t.base);
    assert(t.base.is_open(&t.base) == false);

    /* fd 版本 */
    ret = elog_stdout_transport_init_fd(&t, STDERR_FILENO);
    assert(ret == ELOG_OK);
    t.base.open(&t.base);
    ret = t.base.write(&t.base, (const uint8_t*)"stderr test\n", 13);
    assert(ret == 13);
    t.base.close(&t.base);
}

static void test_dispatch_null(void) {
    printf("  test_dispatch_null...\n");

    elog_transport_registry_t r;
    elog_transport_registry_init(&r);

    /* NULL data 不应崩溃 */
    elog_transport_dispatch(&r, NULL, 0);
    elog_transport_dispatch(&r, (const uint8_t*)"x", 0);

    /* NULL registry */
    elog_transport_dispatch(NULL, (const uint8_t*)"x", 1);
    elog_transport_flush_all(NULL);

    elog_transport_registry_destroy(&r);
}

int test_elog_transport(void) {
    printf("test_elog_transport:\n");

    test_registry_init();
    test_registry_register_unregister();
    test_registry_dispatch();
    test_registry_flush_all();
    test_registry_max();
    test_stdout_transport();
    test_dispatch_null();

    return 0;
}
