// SPDX-License-Identifier: GPL-2.0
/*
 * demo_importer.c - Character device driver that imports dma-bufs,
 *                   processes them (checksum), and provides sync_file
 *                   based completion.
 *
 * /dev/demo_imp provides three ioctls:
 *   DEMO_IMP_IMPORT   - import a dma-buf fd (attach + map)
 *   DEMO_IMP_PROCESS  - compute checksum, return completion fence
 *   DEMO_IMP_UNIMPORT - detach + unmap + release
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/dma-fence.h>
#include <linux/sync_file.h>
#include <linux/dma-mapping.h>

#include "demo_importer.h"

/* Device pointer used for dma_buf_attach; set during module_init */
static struct device *demo_imp_dma_dev;

/* ------------------------------------------------------------------ */
/* Simple completion fence                                             */
/* ------------------------------------------------------------------ */

struct demo_completion_fence {
	struct dma_fence base;
	spinlock_t lock;
};

static inline struct demo_completion_fence *
to_demo_completion_fence(struct dma_fence *f)
{
	return container_of(f, struct demo_completion_fence, base);
}

static const char *demo_comp_get_driver_name(struct dma_fence *fence)
{
	return "demo_comp";
}

static const char *demo_comp_get_timeline_name(struct dma_fence *fence)
{
	return "demo_comp";
}

static bool demo_comp_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void demo_comp_release(struct dma_fence *fence)
{
	kfree(to_demo_completion_fence(fence));
}

static const struct dma_fence_ops demo_comp_fence_ops = {
	.get_driver_name	= demo_comp_get_driver_name,
	.get_timeline_name	= demo_comp_get_timeline_name,
	.enable_signaling	= demo_comp_enable_signaling,
	.wait			= dma_fence_default_wait,
	.release		= demo_comp_release,
};

static struct dma_fence *demo_comp_fence_create(void)
{
	struct demo_completion_fence *cf;

	cf = kzalloc(sizeof(*cf), GFP_KERNEL);
	if (!cf)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&cf->lock);

	dma_fence_init(&cf->base, &demo_comp_fence_ops, &cf->lock,
		       dma_fence_context_alloc(1), 1);

	return &cf->base;
}

/* ------------------------------------------------------------------ */
/* Per-open-file import session                                        */
/* ------------------------------------------------------------------ */

struct demo_import_session {
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	bool mapped;
};

static void demo_import_session_free(struct demo_import_session *sess)
{
	if (!sess)
		return;

	if (sess->mapped && sess->sgt) {
		dma_buf_unmap_attachment(sess->attach, sess->sgt,
						  DMA_BIDIRECTIONAL);
	}
	if (sess->attach)
		dma_buf_detach(sess->dmabuf, sess->attach);
	if (sess->dmabuf)
		dma_buf_put(sess->dmabuf);

	kfree(sess);
}

/* ------------------------------------------------------------------ */
/* file_operations                                                     */
/* ------------------------------------------------------------------ */

static int demo_imp_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int demo_imp_release(struct inode *inode, struct file *filp)
{
	struct demo_import_session *sess = filp->private_data;

	demo_import_session_free(sess);
	filp->private_data = NULL;

	return 0;
}

static long demo_imp_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	struct demo_import_session *sess = filp->private_data;
	long ret = 0;
	switch (cmd) {
	case DEMO_IMP_IMPORT: {
		struct demo_imp_import_req req;
		struct dma_buf *dmabuf;
		struct dma_buf_attachment *attach;
		struct sg_table *sgt;

		if (sess) {
			/* Already have an imported buffer */
			return -EBUSY;
		}

		if (copy_from_user(&req, uarg, sizeof(req)))
			return -EFAULT;

		dmabuf = dma_buf_get(req.dmabuf_fd);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);

		attach = dma_buf_attach(dmabuf, demo_imp_dma_dev);
		if (IS_ERR(attach)) {
			dma_buf_put(dmabuf);
			return PTR_ERR(attach);
		}

		sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
		if (IS_ERR(sgt)) {
			dma_buf_detach(dmabuf, attach);
			dma_buf_put(dmabuf);
			return PTR_ERR(sgt);
		}

		sess = kzalloc(sizeof(*sess), GFP_KERNEL);
		if (!sess) {
			dma_buf_unmap_attachment(attach, sgt,
							  DMA_BIDIRECTIONAL);
			dma_buf_detach(dmabuf, attach);
			dma_buf_put(dmabuf);
			return -ENOMEM;
		}

		sess->dmabuf = dmabuf;
		sess->attach = attach;
		sess->sgt = sgt;
		sess->mapped = true;
		filp->private_data = sess;

		req.num_sg_entries = sgt->nents;

		if (copy_to_user(uarg, &req, sizeof(req)))
			return -EFAULT;

		break;
	}

	case DEMO_IMP_PROCESS: {
		struct demo_imp_process_req req;
		struct dma_buf *dmabuf;
		struct dma_fence *fence;
		struct iosys_map map;
		u8 *data;
		size_t len;
		u32 csum = 0;
		size_t i;
		int sync_fd;

		if (!sess) {
			return -EINVAL;
		}

		if (copy_from_user(&req, uarg, sizeof(req)))
			return -EFAULT;

		dmabuf = sess->dmabuf;

		/* Wait for outstanding WRITE fences */
		ret = dma_resv_wait_timeout(dmabuf->resv, DMA_RESV_USAGE_WRITE,
					    true, MAX_SCHEDULE_TIMEOUT);
		if (ret <= 0)
			return ret == 0 ? -EBUSY : ret;

		/* Create completion fence */
		fence = demo_comp_fence_create();
		if (IS_ERR(fence))
			return PTR_ERR(fence);

		/* Reserve space for the BOOKKEEP fence */
		ret = dma_resv_reserve_fences(dmabuf->resv, 1);
		if (ret) {
			dma_fence_put(fence);
			return ret;
		}

		/* Add as BOOKKEEP fence so consumers know we processed it */
		dma_resv_add_fence(dmabuf->resv, fence, DMA_RESV_USAGE_BOOKKEEP);

		/* Compute checksum via CPU access */
		ret = dma_buf_begin_cpu_access(dmabuf, DMA_FROM_DEVICE);
		if (ret) {
			dma_fence_put(fence);
			return ret;
		}

		ret = dma_buf_vmap(dmabuf, &map);
		if (ret) {
			dma_buf_end_cpu_access(dmabuf, DMA_FROM_DEVICE);
			dma_fence_put(fence);
			return ret;
		}

		data = map.vaddr;
		len = dmabuf->size;
		for (i = 0; i < len; i++)
			csum += data[i];

		dma_buf_vunmap(dmabuf, &map);
		dma_buf_end_cpu_access(dmabuf, DMA_FROM_DEVICE);

		/* Signal completion */
		dma_fence_signal(fence);

		/* Create sync_file */
		sync_fd = get_unused_fd_flags(O_CLOEXEC);
		if (sync_fd < 0) {
			dma_fence_put(fence);
			return sync_fd;
		}

		{
			struct sync_file *sync_file = sync_file_create(fence);
			if (!sync_file) {
				put_unused_fd(sync_fd);
				dma_fence_put(fence);
				return -ENOMEM;
			}
			fd_install(sync_fd, sync_file->file);
		}

		req.checksum = csum;
		req.out_fence_fd = sync_fd;
		dma_fence_put(fence);

		if (copy_to_user(uarg, &req, sizeof(req)))
			return -EFAULT;

		break;
	}

	case DEMO_IMP_UNIMPORT: {
		if (!sess)
			return -EINVAL;

		filp->private_data = NULL;
		demo_import_session_free(sess);
		sess = NULL;

		break;
	}

	default:
		return -ENOTTY;
	}

	return ret;
}

static const struct file_operations demo_imp_fops = {
	.owner		= THIS_MODULE,
	.open		= demo_imp_open,
	.release	= demo_imp_release,
	.unlocked_ioctl	= demo_imp_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

/* ------------------------------------------------------------------ */
/* miscdevice + module init/exit                                       */
/* ------------------------------------------------------------------ */

static struct miscdevice demo_imp_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "demo_imp",
	.fops	= &demo_imp_fops,
};

static int __init demo_imp_init(void)
{
	int ret;

	ret = misc_register(&demo_imp_misc);
	if (ret)
		return ret;

	/* Use the misc device's embedded struct device for DMA mapping */
	demo_imp_dma_dev = demo_imp_misc.this_device;

	return 0;
}

static void __exit demo_imp_exit(void)
{
	misc_deregister(&demo_imp_misc);
}

module_init(demo_imp_init);
module_exit(demo_imp_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Demo dma-buf importer");
MODULE_AUTHOR("demo");
MODULE_IMPORT_NS(DMA_BUF);
