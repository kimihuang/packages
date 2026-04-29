/*
 * demo_app.c - DMA-BUF demo application
 *
 * Demonstrates a full DMA-BUF pipeline with both implicit and explicit
 * synchronization paths, using a demo exporter and demo importer.
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
#include <linux/dma-buf.h>

/* ======================================================================
 * Manual definitions for kernel structures/ioctls that may not be in
 * older userspace headers.
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

#define FENCE_TIMEOUT_MS 5000

/*
 * wait_fence - Poll on an explicit fence fd with timeout.
 * Returns 0 on success (fence signaled), negative on error/timeout.
 */
static int wait_fence(int fence_fd, const char *label)
{
    struct pollfd pfd = {
        .fd = fence_fd,
        .events = POLLIN,
    };

    int ret = poll(&pfd, 1, FENCE_TIMEOUT_MS);
    if (ret < 0) {
        perror(label);
        return -errno;
    }
    if (ret == 0) {
        fprintf(stderr, "[WARN] %s: poll timed out after %d ms\n",
            label, FENCE_TIMEOUT_MS);
        return -ETIMEDOUT;
    }
    printf("[OK]   %s: fence signaled\n", label);
    return 0;
}

/*
 * wait_dmabuf_resv - Export reservation fences from a dma-buf and wait.
 * This exercises the implicit sync path by extracting fences attached
 * to the dma-buf reservation object.
 */
static int wait_dmabuf_resv(int dmabuf_fd, const char *label)
{
    struct dma_buf_export_sync_file exp = {
        .flags = DMA_BUF_SYNC_WRITE,  /* wait for write completion */
        .fd = -1,
    };

    int ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp);
    if (ret < 0) {
        /* If no pending fences, kernel may return -ENOENT or similar */
        if (errno == ENOENT || errno == EINVAL) {
            printf("[OK]   %s: no pending resv fences\n", label);
            return 0;
        }
        perror(label);
        return -errno;
    }

    printf("[INFO] %s: exported resv fence fd=%d, waiting...\n", label, exp.fd);
    int rc = wait_fence(exp.fd, label);
    close(exp.fd);
    return rc;
}

/*
 * import_fence_to_resv - Import an explicit fence into a dma-buf's
 * reservation object (implicit sync binding).
 */
static int import_fence_to_resv(int dmabuf_fd, int fence_fd,
                __u32 flags, const char *label)
{
    struct dma_buf_import_sync_file imp = {
        .flags = flags,
        .fd = fence_fd,
    };

    int ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &imp);
    if (ret < 0) {
        perror(label);
        return -errno;
    }
    printf("[OK]   %s: imported fence fd=%d into resv (flags=0x%x)\n",
           label, fence_fd, flags);
    return 0;
}

/* ======================================================================
 * main
 * ====================================================================== */

int main(void)
{
    int ret;
    int heap_fd = -1, exp_fd = -1, imp_fd = -1;
    int dmabuf_fd = -1, fence_fd = -1;
    void *map = MAP_FAILED;
    __u8 *buf;
    const size_t buf_size = 4096;

    printf("=== DMA-BUF Demo Application ===\n\n");

    /* ------------------------------------------------------------------
     * Step 1: Allocate from demo heap
     * ------------------------------------------------------------------ */
    printf("-- Step 1: Allocate DMA-BUF from /dev/dma_heap/demo --\n");

    heap_fd = open("/dev/dma_heap/demo", O_RDWR);
    if (heap_fd < 0) {
        perror("open /dev/dma_heap/demo");
        ret = -errno;
        goto cleanup;
    }

    struct dma_heap_allocation_data alloc = {
        .len = buf_size,
        .fd_flags = O_RDWR | O_CLOEXEC,
        .heap_flags = 0,
    };

    ret = ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc);
    if (ret < 0) {
        perror("DMA_HEAP_IOCTL_ALLOC");
        ret = -errno;
        goto cleanup;
    }
    dmabuf_fd = alloc.fd;
    printf("[OK]   Allocated dmabuf fd=%d, size=%zu\n\n", dmabuf_fd, buf_size);

    /* ------------------------------------------------------------------
     * Step 2: mmap, write pattern, cache flush
     * ------------------------------------------------------------------ */
    printf("-- Step 2: mmap, write 0xAA pattern, cache flush --\n");

    map = mmap(NULL, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd, 0);
    if (map == MAP_FAILED) {
        perror("mmap");
        ret = -errno;
        goto cleanup;
    }

    memset(map, 0xAA, buf_size);

    struct dma_buf_sync sync_start = {
        .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE,
    };
    ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync_start);
    if (ret < 0) {
        perror("DMA_BUF_IOCTL_SYNC (START WRITE)");
        ret = -errno;
        goto cleanup;
    }

    struct dma_buf_sync sync_end = {
        .flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE,
    };
    ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync_end);
    if (ret < 0) {
        perror("DMA_BUF_IOCTL_SYNC (END WRITE)");
        ret = -errno;
        goto cleanup;
    }
    printf("[OK]   Wrote 0xAA pattern, cache flushed\n\n");

    /* ------------------------------------------------------------------
     * Step 3: DMA fill via exporter
     * ------------------------------------------------------------------ */
    printf("-- Step 3: DMA fill via /dev/demo_exp --\n");

    exp_fd = open("/dev/demo_exp", O_RDWR);
    if (exp_fd < 0) {
        perror("open /dev/demo_exp");
        ret = -errno;
        goto cleanup;
    }

    struct demo_exp_fill_req fill = {
        .dmabuf_fd = dmabuf_fd,
        .fill_pattern = 0x55,
        .delay_ms = 10,
        .out_fence_fd = -1,
    };

    ret = ioctl(exp_fd, DEMO_EXP_DMA_FILL, &fill);
    if (ret < 0) {
        perror("DEMO_EXP_DMA_FILL");
        ret = -errno;
        goto cleanup;
    }
    fence_fd = fill.out_fence_fd;
    printf("[OK]   DMA fill started, out_fence_fd=%d\n\n", fence_fd);

    /* ------------------------------------------------------------------
     * Step 4: Wait on explicit fence
     * ------------------------------------------------------------------ */
    printf("-- Step 4: Wait explicit fence --\n");

    if (fence_fd >= 0) {
        ret = wait_fence(fence_fd, "explicit fence (DMA fill)");
        close(fence_fd);
        fence_fd = -1;
        if (ret < 0)
            goto cleanup;
    } else {
        printf("[WARN] No out-fence returned, skipping explicit wait\n");
    }
    printf("\n");

    /* ------------------------------------------------------------------
     * Step 5: Wait implicit sync (resv fences)
     * ------------------------------------------------------------------ */
    printf("-- Step 5: Wait implicit sync (resv export) --\n");

    ret = wait_dmabuf_resv(dmabuf_fd, "implicit sync (DMA fill)");
    if (ret < 0)
        goto cleanup;
    printf("\n");

    /* ------------------------------------------------------------------
     * Step 6: Read back and verify
     * ------------------------------------------------------------------ */
    printf("-- Step 6: Read back and verify --\n");

    buf = (__u8 *)map;
    if (buf[0] != 0x55) {
        fprintf(stderr, "[FAIL] Expected 0x55, got 0x%02x\n", buf[0]);
        ret = -1;
        goto cleanup;
    }
    printf("[OK]   First byte verified: 0x%02x\n\n", buf[0]);

    /* ------------------------------------------------------------------
     * Step 7: Import to demo importer
     * ------------------------------------------------------------------ */
    printf("-- Step 7: Import to /dev/demo_imp --\n");

    imp_fd = open("/dev/demo_imp", O_RDWR);
    if (imp_fd < 0) {
        perror("open /dev/demo_imp");
        ret = -errno;
        goto cleanup;
    }

    struct demo_imp_import_req imp = {
        .dmabuf_fd = dmabuf_fd,
    };

    ret = ioctl(imp_fd, DEMO_IMP_IMPORT, &imp);
    if (ret < 0) {
        perror("DEMO_IMP_IMPORT");
        ret = -errno;
        goto cleanup;
    }
    printf("[OK]   Imported dmabuf to importer\n\n");

    /* ------------------------------------------------------------------
     * Step 8: Process and get checksum
     * ------------------------------------------------------------------ */
    printf("-- Step 8: Process and wait for completion --\n");

    struct demo_imp_process_req proc = {
        .out_fence_fd = -1,
    };

    ret = ioctl(imp_fd, DEMO_IMP_PROCESS, &proc);
    if (ret < 0) {
        perror("DEMO_IMP_PROCESS");
        ret = -errno;
        goto cleanup;
    }
    fence_fd = proc.out_fence_fd;

    if (fence_fd >= 0) {
        ret = wait_fence(fence_fd, "explicit fence (process)");
        close(fence_fd);
        fence_fd = -1;
        if (ret < 0)
            goto cleanup;
    } else {
        printf("[WARN] No out-fence from process, skipping wait\n");
    }

    printf("[OK]   Processing complete, checksum=0x%08x\n\n", proc.checksum);

    /* ------------------------------------------------------------------
     * Step 9: Verify all READ fences cleared via EXPORT_SYNC_FILE
     * ------------------------------------------------------------------ */
    printf("-- Step 9: Verify READ fences cleared --\n");

    {
        struct dma_buf_export_sync_file exp_chk = {
            .flags = DMA_BUF_SYNC_READ,
            .fd = -1,
        };

        ret = ioctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &exp_chk);
        if (ret < 0) {
            if (errno == ENOENT || errno == EINVAL) {
                printf("[OK]   No pending READ fences (clean)\n\n");
                ret = 0;
            } else {
                perror("EXPORT_SYNC_FILE (READ)");
                ret = -errno;
                goto cleanup;
            }
        } else {
            /* A fence was returned — wait on it */
            printf("[INFO] Pending READ fence fd=%d, waiting...\n", exp_chk.fd);
            ret = wait_fence(exp_chk.fd, "READ fence cleanup");
            close(exp_chk.fd);
            if (ret < 0)
                goto cleanup;
            printf("\n");
        }
    }

    /* ------------------------------------------------------------------
     * Done
     * ------------------------------------------------------------------ */
    printf("=== All steps completed successfully ===\n");
    ret = 0;

cleanup:
    if (map != MAP_FAILED)
        munmap(map, buf_size);
    if (dmabuf_fd >= 0)
        close(dmabuf_fd);
    if (fence_fd >= 0)
        close(fence_fd);
    if (heap_fd >= 0)
        close(heap_fd);
    if (exp_fd >= 0)
        close(exp_fd);
    if (imp_fd >= 0)
        close(imp_fd);

    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
