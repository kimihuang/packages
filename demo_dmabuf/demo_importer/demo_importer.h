/* SPDX-License-Identifier: GPL-2.0 */
/*
 * demo_importer.h - IOCTL definitions for the demo dmabuf importer driver.
 */

#ifndef _DEMO_IMPORTER_H
#define _DEMO_IMPORTER_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define DEMO_IMP_MAGIC		'I'

#define DEMO_IMP_IMPORT		_IOWR(DEMO_IMP_MAGIC, 1, struct demo_imp_import_req)
#define DEMO_IMP_PROCESS	_IOWR(DEMO_IMP_MAGIC, 2, struct demo_imp_process_req)
#define DEMO_IMP_UNIMPORT	_IO(DEMO_IMP_MAGIC, 3)

struct demo_imp_import_req {
	__s32 dmabuf_fd;
	__u32 num_sg_entries;
};

struct demo_imp_process_req {
	__u32 mode;
	__u32 checksum;
	__s32 out_fence_fd;
};

#endif /* _DEMO_IMPORTER_H */
