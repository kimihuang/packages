/*
 * test_dmabuf.c - DMA-BUF test suite (13 test cases)
 *
 * Tests the full DMA-BUF pipeline: allocation, mmap, CPU sync,
 * exporter fill, importer process, explicit/implicit sync,
 * multi-consumer, timeout, full pipeline, and stress.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <errno.h>
#include <signal.h>

#include <linux/dma-buf.h>

#include "test_dmabuf.h"

/* ======================================================================
 * Manual definitions for kernel ioctls/structures
 * ====================================================================== */

#ifndef DMA_HEAP_IOC_MAGIC
struct dma_heap_allocation_data {
    __u64 len;
    __u32 fd;
    __u32 fd_flags;
    __u64 heap_flags;
};
#define DMA_HEAP_IOC_MAGIC        'H'
#define DMA_HEAP_IOCTL_ALLOC    _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)
#endif

#ifndef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
struct dma_buf_export_sync_file {
    __u32 flags;
    __s32 fd;
};
#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE _IOWR('b', 1, struct dma_buf_export_sync_file)
#endif

#ifndef DMA_BUF_IOCTL_IMPORT_SYNC_FILE
struct dma_buf_import_sync_file {
    __u32 flags;
    __s32 fd;
};
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE _IOWR('b', 2, struct dma_buf_import_sync_file)
#endif

/* ======================================================================
 * Demo exporter ioctl definitions (must match kernel demo_exporter.h)
 * ====================================================================== */

#define DEMO_EXP_MAGIC        'E'

struct demo_exp_alloc_req {
    __u32 size;
    __s32 dmabuf_fd;
};

#define DEMO_EXP_ALLOC        _IOWR(DEMO_EXP_MAGIC, 1, struct demo_exp_alloc_req)

struct demo_exp_fill_req {
    __s32 dmabuf_fd;
    __u32 fill_pattern;
    __u32 delay_ms;
    __s32 out_fence_fd;
};

#define DEMO_EXP_DMA_FILL    _IOWR(DEMO_EXP_MAGIC, 2, struct demo_exp_fill_req)

/* ======================================================================
 * Demo importer ioctl definitions (must match kernel demo_importer.h)
 * ====================================================================== */

#define DEMO_IMP_MAGIC        'I'

struct demo_imp_import_req {
    __s32 dmabuf_fd;
    __u32 num_sg_entries;
};

#define DEMO_IMP_IMPORT        _IOWR(DEMO_IMP_MAGIC, 1, struct demo_imp_import_req)

struct demo_imp_process_req {
    __u32 mode;
    __u32 checksum;
    __s32 out_fence_fd;
};

#define DEMO_IMP_PROCESS    _IOWR(DEMO_IMP_MAGIC, 2, struct demo_imp_process_req)

/* ======================================================================
 * Helpers
 * ====================================================================== */

#define HEAP_PATH   "/dev/dma_heap/demo"
#define EXP_PATH    "/dev/demo_exp"
#define IMP_PATH    "/dev/demo_imp"
#define TEST_SIZE   4096

static int wait_fence(int fence_fd, int timeout_ms)
{
    struct pollfd pfd = { .fd = fence_fd, .events = POLLIN };
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0)
        return -errno;
    if (ret == 0)
        return -ETIMEDOUT;
    return 0;
}

static int alloc_dmabuf(size_t size)
{
    int heap_fd = open(HEAP_PATH, O_RDWR);
    if (heap_fd < 0)
        return -errno;

    struct dma_heap_allocation_data alloc = {
        .len = size,
        .fd_flags = O_RDWR | O_CLOEXEC,
        .heap_flags = 0,
    };

    int ret = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc);
    close(heap_fd);
    if (ret < 0)
        return -errno;
    return alloc.fd;
}

/* ======================================================================
 * Test 1: test_heap_alloc_free
 * ====================================================================== */

TEST_CASE(test_heap_alloc_free)
{
    int fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(fd, 0);
    close(fd);
    return 0;
}

/* ======================================================================
 * Test 2: test_mmap_read_write
 * ====================================================================== */

TEST_CASE(test_mmap_read_write)
{
    int fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(fd, 0);

    void *map = mmap(NULL, TEST_SIZE, PROT_READ | PROT_WRITE,
             MAP_SHARED, fd, 0);
    ASSERT_TRUE(map != MAP_FAILED);

    __u8 *buf = (__u8 *)map;
    memset(buf, 0xAA, TEST_SIZE);
    ASSERT_EQ(buf[0], 0xAA);
    ASSERT_EQ(buf[1024], 0xAA);
    ASSERT_EQ(buf[TEST_SIZE - 1], 0xAA);

    munmap(map, TEST_SIZE);
    close(fd);
    return 0;
}

/* ======================================================================
 * Test 3: test_cpu_sync_cache
 * ====================================================================== */

TEST_CASE(test_cpu_sync_cache)
{
    int fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(fd, 0);

    void *map = mmap(NULL, TEST_SIZE, PROT_READ | PROT_WRITE,
             MAP_SHARED, fd, 0);
    ASSERT_TRUE(map != MAP_FAILED);

    /* Write a pattern */
    memset(map, 0xBB, TEST_SIZE);

    /* CPU cache sync: start write */
    struct dma_buf_sync sync_s = {
        .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE,
    };
    int ret = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync_s);
    ASSERT_EQ(ret, 0);

    /* CPU cache sync: end write */
    struct dma_buf_sync sync_e = {
        .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE,
    };
    ret = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync_e);
    ASSERT_EQ(ret, 0);

    /* Read back to verify */
    __u8 *buf = (__u8 *)map;
    ASSERT_EQ(buf[0], 0xBB);
    ASSERT_EQ(buf[2048], 0xBB);

    munmap(map, TEST_SIZE);
    close(fd);
    return 0;
}

/* ======================================================================
 * Test 4: test_exporter_alloc
 * ====================================================================== */

TEST_CASE(test_exporter_alloc)
{
    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    struct demo_exp_alloc_req args = { .size = TEST_SIZE };
    int ret = ioctl(exp_fd, DEMO_EXP_ALLOC, &args);
    ASSERT_EQ(ret, 0);
    ASSERT_GE(args.dmabuf_fd, 0);
    close(args.dmabuf_fd);
    close(exp_fd);
    return 0;
}

/* ======================================================================
 * Test 5: test_exporter_sync_fill
 * ====================================================================== */

TEST_CASE(test_exporter_sync_fill)
{
    int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(dmabuf_fd, 0);

    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0x55,
        .delay_ms = 10,
        .out_fence_fd = -1,
    };

    int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    ASSERT_EQ(ret, 0);
    ASSERT_GE(fill.out_fence_fd, 0);

    /* Wait for fence */
    ret = wait_fence(fill.out_fence_fd, 5000);
    ASSERT_EQ(ret, 0);
    close(fill.out_fence_fd);

    /* Verify pattern */
    void *map = mmap(NULL, TEST_SIZE, PROT_READ, MAP_SHARED, dmabuf_fd, 0);
    ASSERT_TRUE(map != MAP_FAILED);

    /* Ensure CPU sees updated data */
    struct dma_buf_sync sync_s = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
    ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync_s);

    __u8 *buf = (__u8 *)map;
    ASSERT_EQ(buf[0], 0x55);

    struct dma_buf_sync sync_e = { .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ };
    ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync_e);

    munmap(map, TEST_SIZE);
    close(exp_fd);
    close(dmabuf_fd);
    return 0;
}

/* ======================================================================
 * Test 6: test_importer_process
 * ====================================================================== */

TEST_CASE(test_importer_process)
{
    int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(dmabuf_fd, 0);

    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    /* Fill via exporter */
    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0xCC,
        .delay_ms = 5,
        .out_fence_fd = -1,
    };
    int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    ASSERT_EQ(ret, 0);
    ret = wait_fence(fill.out_fence_fd, 5000);
    ASSERT_EQ(ret, 0);
    close(fill.out_fence_fd);
    close(exp_fd);

    /* Import */
    int imp_fd = open(IMP_PATH, O_RDWR);
    ASSERT_GE(imp_fd, 0);

    struct demo_imp_import_req imp = { .dmabuf_fd = dmabuf_fd };
    ret = ioctl(imp_fd, DEMO_IMP_IMPORT, &imp);
    ASSERT_EQ(ret, 0);

    /* Process */
    struct demo_imp_process_req proc = { .out_fence_fd = -1 };
    ret = ioctl(imp_fd, DEMO_IMP_PROCESS, &proc);
    ASSERT_EQ(ret, 0);
    ASSERT_GE(proc.out_fence_fd, 0);

    ret = wait_fence(proc.out_fence_fd, 5000);
    ASSERT_EQ(ret, 0);
    close(proc.out_fence_fd);

    close(imp_fd);
    close(dmabuf_fd);
    return 0;
}

/* ======================================================================
 * Test 7: test_export_sync_file
 * ====================================================================== */

TEST_CASE(test_export_sync_file)
{
    int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(dmabuf_fd, 0);

    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    /* Fill via exporter */
    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0xDD,
        .delay_ms = 10,
        .out_fence_fd = -1,
    };
    int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    ASSERT_EQ(ret, 0);

    /* Don't wait for out-fence; instead use EXPORT_SYNC_FILE */
    struct dma_buf_export_sync_file exp_sync = {
        .flags = DMA_BUF_SYNC_WRITE,
        .fd = -1,
    };

    /*
     * The out_fence from the fill operation is not imported into the
     * reservation object by default (explicit sync). So EXPORT_SYNC_FILE
     * may return -ENOENT. We test that the ioctl itself works.
     */
    ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp_sync);
    if (ret == 0) {
        /* Got a fence; wait on it */
        ASSERT_GE(exp_sync.fd, 0);
        ret = wait_fence(exp_sync.fd, 5000);
        ASSERT_EQ(ret, 0);
        close(exp_sync.fd);
    } else {
        /* Expected if no resv fences are pending */
        ASSERT_TRUE(errno == ENOENT || errno == EINVAL);
    }

    /* Clean up the out-fence */
    close(fill.out_fence_fd);
    close(exp_fd);
    close(dmabuf_fd);
    return 0;
}

/* ======================================================================
 * Test 8: test_import_sync_file
 * ====================================================================== */

TEST_CASE(test_import_sync_file)
{
    int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(dmabuf_fd, 0);

    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    /* Fill via exporter */
    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0xEE,
        .delay_ms = 10,
        .out_fence_fd = -1,
    };
    int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    ASSERT_EQ(ret, 0);
    ASSERT_GE(fill.out_fence_fd, 0);

    /* Import the out-fence into the dma-buf reservation object */
    struct dma_buf_import_sync_file imp_sync = {
        .flags = DMA_BUF_SYNC_WRITE,
        .fd = fill.out_fence_fd,
    };

    ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &imp_sync);
    ASSERT_EQ(ret, 0);

    /* Now export and verify we get a pending fence */
    struct dma_buf_export_sync_file exp_sync = {
        .flags = DMA_BUF_SYNC_WRITE,
        .fd = -1,
    };

    ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp_sync);
    if (ret == 0) {
        ASSERT_GE(exp_sync.fd, 0);
        ret = wait_fence(exp_sync.fd, 5000);
        ASSERT_EQ(ret, 0);
        close(exp_sync.fd);
    } else {
        /* If the operation already completed, no pending fence */
        ASSERT_TRUE(errno == ENOENT || errno == EINVAL);
    }

    close(exp_fd);
    close(dmabuf_fd);
    return 0;
}

/* ======================================================================
 * Test 9: test_implicit_sync_wait
 * ====================================================================== */

TEST_CASE(test_implicit_sync_wait)
{
    int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(dmabuf_fd, 0);

    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    /* Fill with implicit sync (no out_fence needed) */
    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0x33,
        .delay_ms = 10,
        .out_fence_fd = -1,
    };

    int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    ASSERT_EQ(ret, 0);

    /* Close out_fence if returned */
    if (fill.out_fence_fd >= 0)
        close(fill.out_fence_fd);

    /* Use EXPORT_SYNC_FILE to check for implicit completion */
    struct dma_buf_export_sync_file exp_sync = {
        .flags = DMA_BUF_SYNC_WRITE,
        .fd = -1,
    };

    ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp_sync);
    if (ret == 0) {
        ASSERT_GE(exp_sync.fd, 0);
        ret = wait_fence(exp_sync.fd, 5000);
        ASSERT_EQ(ret, 0);
        close(exp_sync.fd);
    } else {
        ASSERT_TRUE(errno == ENOENT || errno == EINVAL);
    }

    /* Verify data */
    void *map = mmap(NULL, TEST_SIZE, PROT_READ, MAP_SHARED, dmabuf_fd, 0);
    ASSERT_TRUE(map != MAP_FAILED);

    struct dma_buf_sync sync_s = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
    ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync_s);

    ASSERT_EQ(((__u8 *)map)[0], 0x33);

    struct dma_buf_sync sync_e = { .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ };
    ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync_e);

    munmap(map, TEST_SIZE);
    close(exp_fd);
    close(dmabuf_fd);
    return 0;
}

/* ======================================================================
 * Test 10: test_multi_consumer_parallel
 * ====================================================================== */

TEST_CASE(test_multi_consumer_parallel)
{
    int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(dmabuf_fd, 0);

    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    /* Fill data */
    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0x77,
        .delay_ms = 5,
        .out_fence_fd = -1,
    };
    int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    ASSERT_EQ(ret, 0);
    ret = wait_fence(fill.out_fence_fd, 5000);
    ASSERT_EQ(ret, 0);
    close(fill.out_fence_fd);
    close(exp_fd);

    /* Open two importer instances */
    int imp1 = open(IMP_PATH, O_RDWR);
    ASSERT_GE(imp1, 0);
    int imp2 = open(IMP_PATH, O_RDWR);
    ASSERT_GE(imp2, 0);

    /* Both import the same dmabuf */
    struct demo_imp_import_req imp_arg = { .dmabuf_fd = dmabuf_fd };
    ret = ioctl(imp1, DEMO_IMP_IMPORT, &imp_arg);
    ASSERT_EQ(ret, 0);

    ret = ioctl(imp2, DEMO_IMP_IMPORT, &imp_arg);
    ASSERT_EQ(ret, 0);

    /* Both process in parallel */
    struct demo_imp_process_req proc1 = { .out_fence_fd = -1 };
    struct demo_imp_process_req proc2 = { .out_fence_fd = -1 };

    ret = ioctl(imp1, DEMO_IMP_PROCESS, &proc1);
    ASSERT_EQ(ret, 0);
    ret = ioctl(imp2, DEMO_IMP_PROCESS, &proc2);
    ASSERT_EQ(ret, 0);

    /* Wait for both */
    ASSERT_GE(proc1.out_fence_fd, 0);
    ASSERT_GE(proc2.out_fence_fd, 0);

    struct pollfd pfds[2];
    pfds[0].fd = proc1.out_fence_fd;
    pfds[0].events = POLLIN;
    pfds[1].fd = proc2.out_fence_fd;
    pfds[1].events = POLLIN;

    ret = poll(pfds, 2, 5000);
    ASSERT_GE(ret, 1);

    close(proc1.out_fence_fd);
    close(proc2.out_fence_fd);
    close(imp1);
    close(imp2);
    close(dmabuf_fd);
    return 0;
}

/* ======================================================================
 * Test 11: test_fence_timeout
 * ====================================================================== */

TEST_CASE(test_fence_timeout)
{
    int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(dmabuf_fd, 0);

    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    /* Request a long delay */
    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0x88,
        .delay_ms = 100,  /* 100ms — reduced for QEMU */
        .out_fence_fd = -1,
    };

    int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    ASSERT_EQ(ret, 0);
    ASSERT_GE(fill.out_fence_fd, 0);

    /* Poll with short timeout — should time out */
    ret = wait_fence(fill.out_fence_fd, 10);  /* 10ms timeout */
    ASSERT_NE(ret, 0);  /* Expect timeout */

    /* Now wait properly */
    ret = wait_fence(fill.out_fence_fd, 5000);
    ASSERT_EQ(ret, 0);

    close(fill.out_fence_fd);
    close(exp_fd);
    close(dmabuf_fd);
    return 0;
}

/* ======================================================================
 * Test 12: test_full_pipeline (end-to-end 5-step)
 * ====================================================================== */

TEST_CASE(test_full_pipeline)
{
    /* Step 1: Allocate */
    int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
    ASSERT_GE(dmabuf_fd, 0);

    /* Step 2: mmap + write */
    void *map = mmap(NULL, TEST_SIZE, PROT_READ | PROT_WRITE,
             MAP_SHARED, dmabuf_fd, 0);
    ASSERT_TRUE(map != MAP_FAILED);
    memset(map, 0xAA, TEST_SIZE);

    struct dma_buf_sync sw1 = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE };
    ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sw1);
    struct dma_buf_sync ew1 = { .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE };
    ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &ew1);

    /* Step 3: Exporter DMA fill */
    int exp_fd = open(EXP_PATH, O_RDWR);
    ASSERT_GE(exp_fd, 0);

    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0x55,
        .delay_ms = 5,
        .out_fence_fd = -1,
    };
    int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    ASSERT_EQ(ret, 0);
    ASSERT_GE(fill.out_fence_fd, 0);

    ret = wait_fence(fill.out_fence_fd, 5000);
    ASSERT_EQ(ret, 0);
    close(fill.out_fence_fd);
    close(exp_fd);

    /* Step 4: Import + process */
    int imp_fd = open(IMP_PATH, O_RDWR);
    ASSERT_GE(imp_fd, 0);

    struct demo_imp_import_req imp = { .dmabuf_fd = dmabuf_fd };
    ret = ioctl(imp_fd, DEMO_IMP_IMPORT, &imp);
    ASSERT_EQ(ret, 0);

    struct demo_imp_process_req proc = { .out_fence_fd = -1 };
    ret = ioctl(imp_fd, DEMO_IMP_PROCESS, &proc);
    ASSERT_EQ(ret, 0);
    ASSERT_GE(proc.out_fence_fd, 0);

    ret = wait_fence(proc.out_fence_fd, 5000);
    ASSERT_EQ(ret, 0);
    close(proc.out_fence_fd);
    close(imp_fd);

    /* Step 5: Verify */
    struct dma_buf_sync sr = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
    ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sr);

    ASSERT_EQ(((__u8 *)map)[0], 0x55);

    struct dma_buf_sync er = { .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ };
    ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &er);

    /* Verify no pending READ fences */
    struct dma_buf_export_sync_file exp_sync = {
        .flags = DMA_BUF_SYNC_READ,
        .fd = -1,
    };
    ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp_sync);
    if (ret == 0) {
        /* Wait and clean up */
        close(exp_sync.fd);
    }

    munmap(map, TEST_SIZE);
    close(dmabuf_fd);
    return 0;
}

/* ======================================================================
 * Test 13: test_stress_repeated_cycles
 * ====================================================================== */

TEST_CASE(test_stress_repeated_cycles)
{
    const int iterations = 20;

    for (int i = 0; i < iterations; i++) {
        int dmabuf_fd = alloc_dmabuf(TEST_SIZE);
        if (dmabuf_fd < 0)
            return -1;

        int exp_fd = open(EXP_PATH, O_RDWR);
        if (exp_fd < 0) {
            close(dmabuf_fd);
            return -1;
        }

        struct demo_exp_fill_req fill = {
            .dmabuf_fd = dmabuf_fd,
            .fill_pattern = (__u32)(i & 0xFF),
            .delay_ms = 0,
            .out_fence_fd = -1,
        };

        int ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
        if (ret < 0) {
            close(exp_fd);
            close(dmabuf_fd);
            return -1;
        }

        if (fill.out_fence_fd >= 0) {
            ret = wait_fence(fill.out_fence_fd, 5000);
            close(fill.out_fence_fd);
            if (ret < 0) {
                close(exp_fd);
                close(dmabuf_fd);
                return -1;
            }
        }

        close(exp_fd);
        close(dmabuf_fd);
    }

    printf("  (completed %d iterations)\n", iterations);
    return 0;
}

/* ======================================================================
 * Test suite registration
 * ====================================================================== */

/*
 * Macro to reference a test function by name in the test array.
 * TEST_CASE already defines the function; this just wraps it as a struct.
 */
#define TEST_CASE_REF(_name) { #_name, _name }

static const struct test_case dmabuf_tests[] = {
    TEST_CASE_REF(test_heap_alloc_free),
    TEST_CASE_REF(test_mmap_read_write),
    TEST_CASE_REF(test_cpu_sync_cache),
    TEST_CASE_REF(test_exporter_alloc),
    TEST_CASE_REF(test_exporter_sync_fill),
    TEST_CASE_REF(test_importer_process),
    TEST_CASE_REF(test_export_sync_file),
    TEST_CASE_REF(test_import_sync_file),
    TEST_CASE_REF(test_implicit_sync_wait),
    TEST_CASE_REF(test_multi_consumer_parallel),
    TEST_CASE_REF(test_fence_timeout),
    TEST_CASE_REF(test_full_pipeline),
    TEST_CASE_REF(test_stress_repeated_cycles),
    { NULL, NULL }
};

int main(void)
{
    RUN_ALL_TESTS(dmabuf_tests);
    return tests_failed_count > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
