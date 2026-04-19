#ifndef SORT_REGISTRY_H
#define SORT_REGISTRY_H

#include "sort_types.h"

#ifdef __cplusplus
extern "C" {
#endif

SORT_API const sort_algorithm_t *sort_registry_get_all(void);
SORT_API size_t sort_registry_count(void);
SORT_API const sort_algorithm_t *sort_registry_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SORT_REGISTRY_H */
