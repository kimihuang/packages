// SPDX-License-Identifier: GPL-2.0
/*
 * demo_exporter.c - Character device driver that allocates and fills dma-bufs.
 *
 * /dev/demo_exp provides three ioctls:
 *   DEMO_EXP_ALLOC     - allocate a dma-buf and return its fd
 *   DEMO_EXP_DMA_FILL  - schedule a timer-based fill, return sync_file fd
 *   DEMO_EXP_QUERY     - check whether the dma-buf's WRITE fence is signaled
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/dma-fence.h>
#include <linux/sync_file.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>

#include "demo_exporter.h"
#include "demo_timer_fence.c"	/* inlined for single-module build */

/* ------------------------------------------------------------------ */
/* Internal allocation helper (vmalloc + dma_buf_export)               */
/* ------------------------------------------------------------------ */

struct demo_exp_buf {
	struct mutex lock;
	struct list_head attachments;
	size_t size;
	void *vaddr;
	struct page **pages;
	unsigned int num_pages;
	struct dma_buf *dmabuf;
};

/* Minimal dma_buf_ops for internally exported buffers */

struct exp_attachment {
	struct device *dev;
	struct sg_table sgt;
	struct list_head list;
	bool mapped;
};

static int exp_attach(struct dma_buf *dmabuf, struct dma_buf_attachment *att)
{
	struct demo_exp_buf *eb = dmabuf->priv;
	struct exp_attachment *ea;
	struct scatterlist *sg;
	unsigned int i;
	int ret;

	ea = kzalloc(sizeof(*ea), GFP_KERNEL);
	if (!ea)
		return -ENOMEM;

	ret = sg_alloc_table(&ea->sgt, eb->num_pages, GFP_KERNEL);
	if (ret) {
		kfree(ea);
		return ret;
	}

	sg = ea->sgt.sgl;
	for (i = 0; i < eb->num_pages; i++) {
		sg_set_page(sg, eb->pages[i], PAGE_SIZE, 0);
		sg = sg_next(sg);
	}

	ea->dev = att->dev;
	ea->mapped = false;
	INIT_LIST_HEAD(&ea->list);
	att->priv = ea;

	mutex_lock(&eb->lock);
	list_add(&ea->list, &eb->attachments);
	mutex_unlock(&eb->lock);

	return 0;
}

static void exp_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *att)
{
	struct demo_exp_buf *eb = dmabuf->priv;
	struct exp_attachment *ea = att->priv;

	mutex_lock(&eb->lock);
	list_del(&ea->list);
	mutex_unlock(&eb->lock);

	if (ea->mapped)
		dma_unmap_sgtable(att->dev, &ea->sgt, DMA_BIDIRECTIONAL, 0);

	sg_free_table(&ea->sgt);
	kfree(ea);
}

static struct sg_table *exp_map_dma_buf(struct dma_buf_attachment *att,
					   enum dma_data_direction dir)
{
	struct exp_attachment *ea = att->priv;
	int ret;

	ret = dma_map_sgtable(att->dev, &ea->sgt, dir, 0);
	if (ret)
		return ERR_PTR(ret);

	ea->mapped = true;
	return &ea->sgt;
}

static void exp_unmap_dma_buf(struct dma_buf_attachment *att,
				      struct sg_table *sgt,
				      enum dma_data_direction dir)
{
	struct exp_attachment *ea = att->priv;

	dma_unmap_sgtable(att->dev, &ea->sgt, dir, 0);
	ea->mapped = false;
}

static void exp_release(struct dma_buf *dmabuf)
{
	struct demo_exp_buf *eb = dmabuf->priv;

	vfree(eb->vaddr);
	kfree(eb->pages);
	kfree(eb);
}

static int exp_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct demo_exp_buf *eb = dmabuf->priv;
	unsigned long start = vma->vm_start;
	unsigned int i;
	int ret;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;

	for (i = 0; i < eb->num_pages; i++) {
		ret = remap_pfn_range(vma, start,
				      page_to_pfn(eb->pages[i]),
				      PAGE_SIZE, vma->vm_page_prot);
		if (ret)
			return ret;
		start += PAGE_SIZE;
	}

	return 0;
}

static int exp_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct demo_exp_buf *eb = dmabuf->priv;
	void *vaddr;

	vaddr = vmap(eb->pages, eb->num_pages, VM_MAP, PAGE_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	iosys_map_set_vaddr(map, vaddr);
	return 0;
}

static void exp_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	vunmap(map->vaddr);
}

static int exp_begin_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction dir)
{
	struct demo_exp_buf *eb = dmabuf->priv;
	struct exp_attachment *ea;

	mutex_lock(&eb->lock);
	list_for_each_entry(ea, &eb->attachments, list) {
		if (ea->mapped)
			dma_sync_sgtable_for_cpu(ea->dev, &ea->sgt, dir);
	}
	mutex_unlock(&eb->lock);

	return 0;
}

static int exp_end_cpu_access(struct dma_buf *dmabuf,
				      enum dma_data_direction dir)
{
	struct demo_exp_buf *eb = dmabuf->priv;
	struct exp_attachment *ea;

	mutex_lock(&eb->lock);
	list_for_each_entry(ea, &eb->attachments, list) {
		if (ea->mapped)
			dma_sync_sgtable_for_device(ea->dev, &ea->sgt, dir);
	}
	mutex_unlock(&eb->lock);

	return 0;
}

static const struct dma_buf_ops exp_dma_buf_ops = {
	.attach		= exp_attach,
	.detach		= exp_detach,
	.map_dma_buf	= exp_map_dma_buf,
	.unmap_dma_buf	= exp_unmap_dma_buf,
	.release	= exp_release,
	.mmap		= exp_mmap,
	.vmap		= exp_vmap,
	.vunmap		= exp_vunmap,
	.begin_cpu_access = exp_begin_cpu_access,
	.end_cpu_access   = exp_end_cpu_access,
};

static struct dma_buf *demo_exp_alloc_dmabuf(size_t size)
{
	struct demo_exp_buf *eb;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned int num_pages;
	unsigned int i;

	num_pages = DIV_ROUND_UP(size, PAGE_SIZE);

	eb = kzalloc(sizeof(*eb), GFP_KERNEL);
	if (!eb)
		return ERR_PTR(-ENOMEM);

	mutex_init(&eb->lock);
	INIT_LIST_HEAD(&eb->attachments);
	eb->size = size;
	eb->num_pages = num_pages;

	eb->vaddr = vmalloc_user(size);
	if (!eb->vaddr) {
		kfree(eb);
		return ERR_PTR(-ENOMEM);
	}

	eb->pages = kmalloc_array(num_pages, sizeof(*eb->pages), GFP_KERNEL);
	if (!eb->pages) {
		vfree(eb->vaddr);
		kfree(eb);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < num_pages; i++)
		eb->pages[i] = vmalloc_to_page(eb->vaddr + i * PAGE_SIZE);

	exp_info.exp_name = "demo_exporter";
	exp_info.owner = THIS_MODULE;
	exp_info.ops = &exp_dma_buf_ops;
	exp_info.size = size;
	exp_info.priv = eb;

	eb->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(eb->dmabuf)) {
		kfree(eb->pages);
		vfree(eb->vaddr);
		kfree(eb);
		return eb->dmabuf;
	}

	return eb->dmabuf;
}

/* ------------------------------------------------------------------ */
/* file_operations                                                     */
/* ------------------------------------------------------------------ */

static int demo_exp_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int demo_exp_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long demo_exp_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	long ret = 0;

	switch (cmd) {
	case DEMO_EXP_ALLOC: {
		pr_info("demo_exp: DEMO_EXP_ALLOC cmd=%u\n", cmd);
		struct demo_exp_alloc_req req;
		struct dma_buf *dmabuf;
		int fd;

		if (copy_from_user(&req, uarg, sizeof(req)))
			return -EFAULT;

		dmabuf = demo_exp_alloc_dmabuf(req.size);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);

		fd = dma_buf_fd(dmabuf, O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			dma_buf_put(dmabuf);
			return fd;
		}

		req.dmabuf_fd = fd;

		if (copy_to_user(uarg, &req, sizeof(req)))
			return -EFAULT;

		break;
	}

	case DEMO_EXP_DMA_FILL: {
		pr_info("demo_exp: DEMO_EXP_DMA_FILL cmd=%u\n", cmd);
		struct demo_exp_fill_req req;
		struct dma_buf *dmabuf;
		struct dma_fence *fence;
		struct iosys_map map;
		int sync_fd;

		if (copy_from_user(&req, uarg, sizeof(req)))
			return -EFAULT;

		dmabuf = dma_buf_get(req.dmabuf_fd);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);

		/* Wait for any outstanding WRITE fences */
		ret = dma_resv_wait_timeout(dmabuf->resv, DMA_RESV_USAGE_WRITE,
					    true, MAX_SCHEDULE_TIMEOUT);
		if (ret <= 0) {
			dma_buf_put(dmabuf);
			return ret == 0 ? -EBUSY : ret;
		}

		/*
		 * Synchronously fill the buffer BEFORE adding any WRITE fence.
		 * In Linux 6.1, both dma_buf_begin_cpu_access() and
		 * dma_buf_vmap() internally wait for WRITE fences. We do the
		 * fill here while no WRITE fence is pending.
		 */
		ret = dma_buf_begin_cpu_access(dmabuf, DMA_TO_DEVICE);
		if (ret) {
			dma_buf_put(dmabuf);
			return ret;
		}

		ret = dma_buf_vmap(dmabuf, &map);
		if (ret) {
			dma_buf_end_cpu_access(dmabuf, DMA_TO_DEVICE);
			dma_buf_put(dmabuf);
			return ret;
		}

		memset(map.vaddr, (u8)req.fill_pattern, dmabuf->size);

		dma_buf_vunmap(dmabuf, &map);
		dma_buf_end_cpu_access(dmabuf, DMA_TO_DEVICE);

		pr_info("demo_exp: buffer filled with 0x%02x, creating fence delay=%ums\n",
			req.fill_pattern, req.delay_ms);

		/* Create timer fence (just a delayed signal, no buffer ops) */
		fence = demo_timer_fence_create(req.delay_ms);
		if (IS_ERR(fence)) {
			dma_buf_put(dmabuf);
			return PTR_ERR(fence);
		}

		/* Reserve space for the new WRITE fence */
		ret = dma_resv_reserve_fences(dmabuf->resv, 1);
		if (ret) {
			dma_buf_put(dmabuf);
			dma_fence_put(fence);
			return ret;
		}

		/* Add as WRITE fence to reservation object */
		dma_resv_add_fence(dmabuf->resv, fence, DMA_RESV_USAGE_WRITE);

		/* Create sync_file wrapping the fence */
		sync_fd = get_unused_fd_flags(O_CLOEXEC);
		if (sync_fd < 0) {
			dma_buf_put(dmabuf);
			dma_fence_put(fence);
			return sync_fd;
		}

		{
			struct sync_file *sync_file = sync_file_create(fence);
			if (!sync_file) {
				put_unused_fd(sync_fd);
				dma_buf_put(dmabuf);
				dma_fence_put(fence);
				return -ENOMEM;
			}
			fd_install(sync_fd, sync_file->file);
		}

		req.out_fence_fd = sync_fd;
		dma_fence_put(fence);
		dma_buf_put(dmabuf);

		if (copy_to_user(uarg, &req, sizeof(req)))
			return -EFAULT;

		break;
	}

	case DEMO_EXP_QUERY: {
		struct demo_exp_query_req req;
		struct dma_buf *dmabuf;

		if (copy_from_user(&req, uarg, sizeof(req)))
			return -EFAULT;

		dmabuf = dma_buf_get(req.dmabuf_fd);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);

		req.size = dmabuf->size;
		req.is_signaled = dma_resv_test_signaled(dmabuf->resv,
							  DMA_RESV_USAGE_WRITE);

		dma_buf_put(dmabuf);

		if (copy_to_user(uarg, &req, sizeof(req)))
			return -EFAULT;

		break;
	}

	default:
		pr_info("demo_exp: unknown ioctl cmd=%u\n", cmd);
		return -ENOTTY;
	}

	return ret;
}

static const struct file_operations demo_exp_fops = {
	.owner		= THIS_MODULE,
	.open		= demo_exp_open,
	.release	= demo_exp_release,
	.unlocked_ioctl	= demo_exp_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

/* ------------------------------------------------------------------ */
/* miscdevice                                                          */
/* ------------------------------------------------------------------ */

static struct miscdevice demo_exp_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "demo_exp",
	.fops	= &demo_exp_fops,
};

module_misc_device(demo_exp_misc);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Demo dma-buf exporter");
MODULE_AUTHOR("demo");
MODULE_IMPORT_NS(DMA_BUF);
