/**
 * @file elog_transport_file.h
 * @brief FileTransport — 将日志持久化到本地文件，支持按大小轮转
 *
 * 使用示例:
 *   elog_file_transport_t ft;
 *   elog_file_transport_init(&ft, "app.log", 1*1024*1024, 5);
 *   ft.base.open(&ft.base);
 *   elog_add_transport(&ft);
 *   // ... 日志自动写入文件 ...
 *   elog_remove_transport(&ft);
 *   elog_file_transport_deinit(&ft);
 */

#ifndef ELOG_TRANSPORT_FILE_H
#define ELOG_TRANSPORT_FILE_H

#include "elog_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    elog_transport_t base;       /* vtable (必须在第一个字段) */
    int   fd;                   /* 文件描述符 (-1 = 未打开) */
    bool  is_open_;
    char  filepath[ELOG_FILE_PATH_MAX];
    size_t file_size;           /* 当前文件已写字节数 */
    size_t max_file_size;       /* 单文件最大大小 (超过触发轮转) */
    int   max_files;            /* 保留的最大文件数 (含当前) */
} elog_file_transport_t;

/**
 * 初始化 FileTransport
 * @param t          FileTransport 实例
 * @param filepath   日志文件路径 (NULL 使用默认 ELOG_FILE_DEFAULT_PATH)
 * @param max_size   单文件最大字节数 (0 使用默认 ELOG_FILE_DEFAULT_SIZE)
 * @param max_files  保留的最大文件数 (0 使用默认 ELOG_FILE_MAX_FILES)
 * @return ELOG_OK 或错误码
 */
int elog_file_transport_init(elog_file_transport_t* t, const char* filepath,
                             size_t max_size, int max_files);

/**
 * 反初始化 FileTransport (关闭 fd, 清理资源)
 * 不删除日志文件。
 */
void elog_file_transport_deinit(elog_file_transport_t* t);

/**
 * 清理所有轮转文件 (包括 .1, .2, ... 后缀文件)
 * 仅用于测试清理。
 */
void elog_file_transport_cleanup(elog_file_transport_t* t);

#ifdef __cplusplus
}
#endif

#endif /* ELOG_TRANSPORT_FILE_H */
