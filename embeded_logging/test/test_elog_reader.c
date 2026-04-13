/**
 * @file test_elog_reader.c
 * @brief Reader/ReaderList 单元测试
 */

#define _DEFAULT_SOURCE
#include "elog.h"
#include "elog_reader.h"
#include "elog_buf.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>

/* ===== 辅助 ===== */

static void test_setup(elog_ring_buf_t* rb, elog_reader_list_t* list) {
    elog_ring_buf_init(rb, 4096);
    elog_reader_list_init(list);
}

static void test_cleanup(elog_ring_buf_t* rb, elog_reader_list_t* list) {
    elog_reader_list_destroy(list);
    elog_ring_buf_destroy(rb);
}

static void write_log(elog_ring_buf_t* rb, const char* tag, const char* msg) {
    rb->base.log(&rb->base, ELOG_ID_MAIN, ELOG_LEVEL_INFO, 1, 1, 10, tag, msg);
}

/* ===== 测试用例 ===== */

static void test_reader_create_destroy(void) {
    printf("  test_reader_create_destroy...\n");

    elog_reader_t* r = elog_reader_create(0, 0);
    assert(r != NULL);
    assert(r->read_pos == 0);
    assert(r->min_level == ELOG_LEVEL_VERBOSE);
    assert(r->pid_filter == 0);
    elog_reader_destroy(r);

    r = elog_reader_create(ELOG_READ_NONBLOCK, 1234);
    assert(r != NULL);
    assert((r->mode & ELOG_READ_NONBLOCK) != 0);
    assert(r->pid_filter == 1234);
    elog_reader_destroy(r);

    elog_reader_destroy(NULL);
}

static void test_reader_read_nonblock_empty(void) {
    printf("  test_reader_read_nonblock_empty...\n");

    elog_ring_buf_t rb;
    elog_reader_list_t list;
    test_setup(&rb, &list);

    elog_reader_t* r = elog_reader_create(ELOG_READ_NONBLOCK, 0);
    char buf[512];
    int ret = elog_reader_read(r, (void*)&rb, buf, sizeof(buf), 0);
    assert(ret == 0);
    assert(buf[0] == '\0');

    elog_reader_destroy(r);
    test_cleanup(&rb, &list);
}

static void test_reader_read_after_write(void) {
    printf("  test_reader_read_after_write...\n");

    elog_ring_buf_t rb;
    elog_reader_list_t list;
    test_setup(&rb, &list);

    elog_reader_t* r = elog_reader_create(ELOG_READ_NONBLOCK, 0);
    elog_reader_attach(r, &rb);

    write_log(&rb, "test", "hello");
    write_log(&rb, "test", "world");
    write_log(&rb, "sensor", "temp=25");

    char buf[1024];
    int ret = elog_reader_read(r, (void*)&rb, buf, sizeof(buf), 0);
    assert(ret == 3);
    assert(strstr(buf, "hello") != NULL);
    assert(strstr(buf, "world") != NULL);
    assert(strstr(buf, "temp=25") != NULL);

    ret = elog_reader_read(r, (void*)&rb, buf, sizeof(buf), 0);
    assert(ret == 0);

    elog_reader_destroy(r);
    test_cleanup(&rb, &list);
}

static void test_reader_multiple(void) {
    printf("  test_reader_multiple...\n");

    elog_ring_buf_t rb;
    elog_reader_list_t list;
    test_setup(&rb, &list);

    elog_reader_t* r1 = elog_reader_create(ELOG_READ_NONBLOCK, 0);
    elog_reader_attach(r1, &rb);

    write_log(&rb, "test", "entry1");

    elog_reader_t* r2 = elog_reader_create(ELOG_READ_NONBLOCK, 0);
    elog_reader_attach(r2, &rb);

    write_log(&rb, "test", "entry2");

    char buf1[512], buf2[512];
    int ret1 = elog_reader_read(r1, (void*)&rb, buf1, sizeof(buf1), 0);
    assert(ret1 == 2);

    int ret2 = elog_reader_read(r2, (void*)&rb, buf2, sizeof(buf2), 0);
    assert(ret2 == 1);

    assert(strstr(buf1, "entry1") != NULL);
    assert(strstr(buf1, "entry2") != NULL);
    assert(strstr(buf2, "entry1") == NULL);
    assert(strstr(buf2, "entry2") != NULL);

    write_log(&rb, "test", "entry3");
    ret1 = elog_reader_read(r1, (void*)&rb, buf1, sizeof(buf1), 0);
    ret2 = elog_reader_read(r2, (void*)&rb, buf2, sizeof(buf2), 0);
    assert(ret1 == 1);
    assert(ret2 == 1);

    elog_reader_destroy(r1);
    elog_reader_destroy(r2);
    test_cleanup(&rb, &list);
}

/* 阻塞读取: writer 线程写入到 rb, rb->not_empty 自动 signal */
static void* writer_thread(void* arg) {
    elog_ring_buf_t* rb = (elog_ring_buf_t*)arg;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };  /* 100ms */
    nanosleep(&ts, NULL);
    write_log(rb, "async", "wake-up message");
    return NULL;
}

static void test_reader_blocking_wait(void) {
    printf("  test_reader_blocking_wait...\n");

    elog_ring_buf_t rb;
    elog_reader_list_t list;
    test_setup(&rb, &list);

    elog_reader_t* r = elog_reader_create(0, 0);
    elog_reader_attach(r, &rb);

    pthread_t tid;
    pthread_create(&tid, NULL, writer_thread, &rb);

    char buf[512];
    int ret = elog_reader_read(r, (void*)&rb, buf, sizeof(buf), 5000);
    assert(ret >= 1);
    assert(strstr(buf, "wake-up message") != NULL);

    pthread_join(tid, NULL);
    elog_reader_destroy(r);
    test_cleanup(&rb, &list);
}

static void test_reader_blocking_timeout(void) {
    printf("  test_reader_blocking_timeout...\n");

    elog_ring_buf_t rb;
    elog_reader_list_t list;
    test_setup(&rb, &list);

    elog_reader_t* r = elog_reader_create(0, 0);
    elog_reader_attach(r, &rb);

    char buf[512];
    int ret = elog_reader_read(r, (void*)&rb, buf, sizeof(buf), 200);
    assert(ret == 0);

    elog_reader_destroy(r);
    test_cleanup(&rb, &list);
}

static void test_reader_list_max(void) {
    printf("  test_reader_list_max...\n");

    elog_reader_list_t list;
    elog_reader_list_init(&list);

    elog_reader_t* readers[ELOG_MAX_READERS + 1];
    for (int i = 0; i < ELOG_MAX_READERS; i++) {
        readers[i] = elog_reader_create(ELOG_READ_NONBLOCK, 0);
        assert(elog_reader_list_add(&list, readers[i]) == ELOG_OK);
    }

    readers[ELOG_MAX_READERS] = elog_reader_create(ELOG_READ_NONBLOCK, 0);
    assert(elog_reader_list_add(&list, readers[ELOG_MAX_READERS]) == ELOG_ERR_FULL);

    for (int i = 0; i <= ELOG_MAX_READERS; i++) {
        elog_reader_list_remove(&list, readers[i]);
        elog_reader_destroy(readers[i]);
    }
    assert(list.count == 0);

    elog_reader_list_destroy(&list);
}

int test_elog_reader(void) {
    printf("test_elog_reader:\n");

    test_reader_create_destroy();
    test_reader_read_nonblock_empty();
    test_reader_read_after_write();
    test_reader_multiple();
    test_reader_blocking_wait();
    test_reader_blocking_timeout();
    test_reader_list_max();

    return 0;
}
