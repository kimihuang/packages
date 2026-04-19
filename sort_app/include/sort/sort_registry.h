#ifndef SORT_REGISTRY_H
#define SORT_REGISTRY_H

#include "sort_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 获取所有已注册的排序算法表 */
const sort_algorithm_t *sort_registry_get_all(void);

/* 获取已注册算法数量 */
size_t sort_registry_count(void);

/* 根据名称查找排序算法，未找到返回 NULL */
const sort_algorithm_t *sort_registry_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SORT_REGISTRY_H */
