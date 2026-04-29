/* SPDX-License-Identifier: GPL-2.0 */
/*
 * demo_exporter.h - IOCTL definitions for the demo dmabuf exporter driver.
 */

#ifndef _DEMO_EXPORTER_H
#define _DEMO_EXPORTER_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define DEMO_EXP_MAGIC		'E'

#define DEMO_EXP_ALLOC		_IOWR(DEMO_EXP_MAGIC, 1, struct demo_exp_alloc_req)
#define DEMO_EXP_DMA_FILL	_IOWR(DEMO_EXP_MAGIC, 2, struct demo_exp_fill_req)
#define DEMO_EXP_QUERY		_IOWR(DEMO_EXP_MAGIC, 3, struct demo_exp_query_req)

struct demo_exp_alloc_req {
	__u32 size;
	__s32 dmabuf_fd;
};

struct demo_exp_fill_req {
	__s32 dmabuf_fd;
	__u32 fill_pattern;
	__u32 delay_ms;
	__s32 out_fence_fd;
};

struct demo_exp_query_req {
	__s32 dmabuf_fd;
	__u32 size;
	__u32 is_signaled;
};

#endif /* _DEMO_EXPORTER_H */
