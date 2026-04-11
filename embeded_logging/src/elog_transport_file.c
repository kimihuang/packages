/**
 * @file elog_transport_file.c
 * @brief FileTransport 实现 — 文件追加写入 + 按大小轮转
 *
 * 轮转策略:
 *   当 file_size + len > max_file_size 时:
 *     elog.log → elog.log.1, elog.log.1 → elog.log.2, ...
 *     超过 max_files 的旧文件被删除
 *     然后重新打开空文件
 */

#include "elog_transport_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

/* ---- vtable 实现 ---- */

static int file_open(elog_transport_t* self) {
    elog_file_transport_t* t = (elog_file_transport_t*)self;
    if (!t || t->filepath[0] == '\0') return ELOG_ERR_PARAM;

    t->fd = open(t->filepath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (t->fd < 0) return ELOG_ERR_FULL;

    /* 获取当前文件大小 */
    struct stat st;
    if (fstat(t->fd, &st) == 0) {
        t->file_size = (size_t)st.st_size;
    } else {
        t->file_size = 0;
    }

    t->is_open_ = true;
    return ELOG_OK;
}

static void file_close(elog_transport_t* self) {
    elog_file_transport_t* t = (elog_file_transport_t*)self;
    if (!t) return;
    if (t->fd >= 0) {
        fsync(t->fd);
        close(t->fd);
        t->fd = -1;
    }
    t->is_open_ = false;
}

/* 执行文件轮转: elog.log → elog.log.1 → elog.log.2 → ... */
static void file_rotate(elog_file_transport_t* t) {
    if (t->fd < 0) return;

    /* 关闭当前文件 */
    close(t->fd);
    t->fd = -1;
    t->file_size = 0;

    char old_path[ELOG_FILE_PATH_MAX];
    char new_path[ELOG_FILE_PATH_MAX];

    /* 从最大编号向 1 递减移动 */
    for (int i = t->max_files - 1; i >= 1; i--) {
        if (i == 1) {
            snprintf(old_path, sizeof(old_path), "%s", t->filepath);
        } else {
            snprintf(old_path, sizeof(old_path), "%s.%d", t->filepath, i - 1);
        }
        snprintf(new_path, sizeof(new_path), "%s.%d", t->filepath, i);
        rename(old_path, new_path);
    }

    /* 删除超出 max_files 的旧文件 */
    char del_path[ELOG_FILE_PATH_MAX];
    snprintf(del_path, sizeof(del_path), "%s.%d", t->filepath, t->max_files);
    unlink(del_path);

    /* 重新打开空文件 */
    t->fd = open(t->filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (t->fd >= 0) {
        t->file_size = 0;
    }
}

static int file_write(elog_transport_t* self, const uint8_t* data, size_t len) {
    elog_file_transport_t* t = (elog_file_transport_t*)self;
    if (!t || !data || t->fd < 0) return ELOG_ERR_PARAM;

    /* 检查是否需要轮转 */
    if (t->max_file_size > 0 && (t->file_size + len) > t->max_file_size) {
        file_rotate(t);
        if (t->fd < 0) return ELOG_ERR_FULL;
    }

    ssize_t ret = write(t->fd, data, len);
    if (ret < 0) return ELOG_ERR_FULL;

    t->file_size += (size_t)ret;
    return (int)ret;
}

static int file_flush(elog_transport_t* self) {
    elog_file_transport_t* t = (elog_file_transport_t*)self;
    if (!t || t->fd < 0) return ELOG_ERR_PARAM;
    return fsync(t->fd) == 0 ? ELOG_OK : ELOG_ERR_FULL;
}

static bool file_is_open(elog_transport_t* self) {
    elog_file_transport_t* t = (elog_file_transport_t*)self;
    return t ? t->is_open_ : false;
}

static const char* file_name(elog_transport_t* self) {
    (void)self;
    return "file";
}

/* ---- 公共 API ---- */

int elog_file_transport_init(elog_file_transport_t* t, const char* filepath,
                             size_t max_size, int max_files) {
    if (!t) return ELOG_ERR_PARAM;

    memset(t, 0, sizeof(*t));
    t->fd = -1;
    t->is_open_ = false;

    if (filepath) {
        strncpy(t->filepath, filepath, ELOG_FILE_PATH_MAX - 1);
    } else {
        strncpy(t->filepath, ELOG_FILE_DEFAULT_PATH, ELOG_FILE_PATH_MAX - 1);
    }
    t->filepath[ELOG_FILE_PATH_MAX - 1] = '\0';

    t->max_file_size = (max_size > 0) ? max_size : ELOG_FILE_DEFAULT_SIZE;
    t->max_files = (max_files > 0) ? max_files : ELOG_FILE_MAX_FILES;

    t->base.open    = file_open;
    t->base.close   = file_close;
    t->base.write   = file_write;
    t->base.flush   = file_flush;
    t->base.is_open = file_is_open;
    t->base.name    = file_name;

    return ELOG_OK;
}

void elog_file_transport_deinit(elog_file_transport_t* t) {
    if (!t) return;
    file_close(&t->base);
}

void elog_file_transport_cleanup(elog_file_transport_t* t) {
    if (!t) return;
    file_close(&t->base);

    /* 删除主文件 */
    unlink(t->filepath);

    /* 删除所有轮转文件 */
    char path[ELOG_FILE_PATH_MAX];
    for (int i = 1; i <= t->max_files; i++) {
        snprintf(path, sizeof(path), "%s.%d", t->filepath, i);
        unlink(path);
    }
}
