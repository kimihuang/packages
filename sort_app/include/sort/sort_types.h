#ifndef SORT_TYPES_H
#define SORT_TYPES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 统一的排序函数签名 */
typedef void (*sort_func_t)(int *arr, size_t n);

/* 排序算法元信息，用于注册和查找 */
typedef struct {
    const char *name;       /* 算法名称（用于命令行参数匹配） */
    const char *desc;       /* 算法描述 */
    sort_func_t func;       /* 排序函数指针 */
} sort_algorithm_t;

#ifdef __cplusplus
}
#endif

#endif /* SORT_TYPES_H */
