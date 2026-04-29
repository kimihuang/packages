// SPDX-License-Identifier: GPL-2.0
/*
 * demo_heap.c - Custom dma-heap driver that allocates non-contiguous memory
 *               via alloc_page() and exports it as dma-buf.
 *
 * Registers as /dev/dma_heap/demo.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-resv.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/highmem.h>

/* ------------------------------------------------------------------ */
/* Data structures                                                     */
/* ------------------------------------------------------------------ */

struct demo_attachment {
	struct device *dev;
	struct sg_table sgt;
	struct list_head list;
	bool mapped;
};

struct demo_heap_buffer {
	struct dma_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	size_t len;
	struct page **pages;
	unsigned int num_pages;
	struct sg_table sg_table;
	struct dma_buf *dmabuf;
};

/* ------------------------------------------------------------------ */
/* dma_buf_ops                                                         */
/* ------------------------------------------------------------------ */

static int demo_heap_attach(struct dma_buf *dmabuf,
			    struct dma_buf_attachment *attach)
{
	struct demo_heap_buffer *buf = dmabuf->priv;
	struct demo_attachment *a;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	a->dev = attach->dev;

	ret = sg_alloc_table(&a->sgt, buf->num_pages, GFP_KERNEL);
	if (ret) {
		kfree(a);
		return ret;
	}

	{
		struct scatterlist *sg = a->sgt.sgl;
		unsigned int i;

		for (i = 0; i < buf->num_pages; i++) {
			sg_set_page(sg, buf->pages[i], PAGE_SIZE, 0);
			sg = sg_next(sg);
		}
	}

	a->mapped = false;
	INIT_LIST_HEAD(&a->list);

	mutex_lock(&buf->lock);
	list_add(&a->list, &buf->attachments);
	mutex_unlock(&buf->lock);

	attach->priv = a;

	return 0;
}

static void demo_heap_detach(struct dma_buf *dmabuf,
			     struct dma_buf_attachment *attach)
{
	struct demo_heap_buffer *buf = dmabuf->priv;
	struct demo_attachment *a = attach->priv;

	mutex_lock(&buf->lock);
	list_del(&a->list);
	mutex_unlock(&buf->lock);

	if (a->mapped)
		dma_unmap_sgtable(attach->dev, &a->sgt,
				  DMA_BIDIRECTIONAL, 0);

	sg_free_table(&a->sgt);
	kfree(a);
}

static struct sg_table *demo_heap_map_dma_buf(struct dma_buf_attachment *attach,
					      enum dma_data_direction dir)
{
	struct demo_attachment *a = attach->priv;
	int ret;

	ret = dma_map_sgtable(attach->dev, &a->sgt, dir, 0);
	if (ret)
		return ERR_PTR(ret);

	a->mapped = true;

	return &a->sgt;
}

static void demo_heap_unmap_dma_buf(struct dma_buf_attachment *attach,
				    struct sg_table *sgt,
				    enum dma_data_direction dir)
{
	struct demo_attachment *a = attach->priv;

	dma_unmap_sgtable(attach->dev, &a->sgt, dir, 0);
	a->mapped = false;
}

static void demo_heap_release(struct dma_buf *dmabuf)
{
	struct demo_heap_buffer *buf = dmabuf->priv;
	unsigned int i;

	for (i = 0; i < buf->num_pages; i++)
		if (buf->pages[i])
			__free_page(buf->pages[i]);

	sg_free_table(&buf->sg_table);
	kfree(buf->pages);
	kfree(buf);
}

static int demo_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct demo_heap_buffer *buf = dmabuf->priv;
	unsigned long start = vma->vm_start;
	unsigned int i;
	int ret;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;

	for (i = 0; i < buf->num_pages; i++) {
		ret = remap_pfn_range(vma, start,
				      page_to_pfn(buf->pages[i]),
				      PAGE_SIZE, vma->vm_page_prot);
		if (ret)
			return ret;
		start += PAGE_SIZE;
	}

	return 0;
}

static int demo_heap_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct demo_heap_buffer *buf = dmabuf->priv;
	void *vaddr;

	vaddr = vmap(buf->pages, buf->num_pages, VM_MAP, PAGE_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	iosys_map_set_vaddr(map, vaddr);
	return 0;
}

static void demo_heap_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	vunmap(map->vaddr);
}

static int demo_heap_begin_cpu_access(struct dma_buf *dmabuf,
				      enum dma_data_direction dir)
{
	struct demo_heap_buffer *buf = dmabuf->priv;
	struct demo_attachment *a;

	mutex_lock(&buf->lock);
	list_for_each_entry(a, &buf->attachments, list) {
		if (a->mapped)
			dma_sync_sgtable_for_cpu(a->dev, &a->sgt, dir);
	}
	mutex_unlock(&buf->lock);

	return 0;
}

static int demo_heap_end_cpu_access(struct dma_buf *dmabuf,
				    enum dma_data_direction dir)
{
	struct demo_heap_buffer *buf = dmabuf->priv;
	struct demo_attachment *a;

	mutex_lock(&buf->lock);
	list_for_each_entry(a, &buf->attachments, list) {
		if (a->mapped)
			dma_sync_sgtable_for_device(a->dev, &a->sgt, dir);
	}
	mutex_unlock(&buf->lock);

	return 0;
}

static const struct dma_buf_ops demo_heap_dma_buf_ops = {
	.attach		= demo_heap_attach,
	.detach		= demo_heap_detach,
	.map_dma_buf	= demo_heap_map_dma_buf,
	.unmap_dma_buf	= demo_heap_unmap_dma_buf,
	.release	= demo_heap_release,
	.mmap		= demo_heap_mmap,
	.vmap		= demo_heap_vmap,
	.vunmap	= demo_heap_vunmap,
	.begin_cpu_access = demo_heap_begin_cpu_access,
	.end_cpu_access   = demo_heap_end_cpu_access,
};

/* ------------------------------------------------------------------ */
/* dma_heap_ops                                                        */
/* ------------------------------------------------------------------ */

static struct dma_buf *demo_heap_allocate(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags)
{
	struct demo_heap_buffer *buf;
	struct dma_buf *dmabuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned int num_pages;
	struct scatterlist *sg;
	int ret, i;

	num_pages = DIV_ROUND_UP(len, PAGE_SIZE);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->heap = heap;
	buf->len = len;
	buf->num_pages = num_pages;
	INIT_LIST_HEAD(&buf->attachments);
	mutex_init(&buf->lock);

	/* Allocate page pointer array */
	buf->pages = kmalloc_array(num_pages, sizeof(*buf->pages), GFP_KERNEL);
	if (!buf->pages) {
		ret = -ENOMEM;
		goto err_free_buf;
	}

	/* Allocate physical pages one by one */
	for (i = 0; i < num_pages; i++) {
		buf->pages[i] = alloc_page(GFP_KERNEL);
		if (!buf->pages[i]) {
			ret = -ENOMEM;
			goto err_free_pages;
		}
	}

	/* Build sg table manually (avoids sg_alloc_table_from_pages BUG) */
	ret = sg_alloc_table(&buf->sg_table, num_pages, GFP_KERNEL);
	if (ret)
		goto err_free_pages;

	sg = buf->sg_table.sgl;
	for (i = 0; i < num_pages; i++) {
		sg_set_page(sg, buf->pages[i], PAGE_SIZE, 0);
		sg = sg_next(sg);
	}

	exp_info.exp_name = "demo_heap";
	exp_info.owner = THIS_MODULE;
	exp_info.ops = &demo_heap_dma_buf_ops;
	exp_info.size = len;
	exp_info.priv = buf;
	exp_info.flags = fd_flags;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err_free_sg;
	}

	buf->dmabuf = dmabuf;

	return dmabuf;

err_free_sg:
	sg_free_table(&buf->sg_table);
err_free_pages:
	for (i = i - 1; i >= 0; i--)
		__free_page(buf->pages[i]);
	kfree(buf->pages);
err_free_buf:
	kfree(buf);
	return ERR_PTR(ret);
}

static const struct dma_heap_ops demo_heap_ops = {
	.allocate = demo_heap_allocate,
};

/* ------------------------------------------------------------------ */
/* Module init / exit                                                  */
/* ------------------------------------------------------------------ */

static struct dma_heap *demo_heap_dev;

static int __init demo_heap_init(void)
{
	struct dma_heap_export_info exp_info;

	exp_info.name = "demo";
	exp_info.ops = &demo_heap_ops;
	exp_info.priv = NULL;

	demo_heap_dev = dma_heap_add(&exp_info);
	if (IS_ERR(demo_heap_dev))
		return PTR_ERR(demo_heap_dev);

	return 0;
}

static void __exit demo_heap_exit(void)
{
	/*
	 * In Linux 6.1, there is no dma_heap_put() API.
	 * The heap device is cleaned up when the module refcount
	 * drops to zero and the kernel reclaims it.
	 *
	 * Note: rmmod will fail if any dma-buf fd is still open
	 * because exp_info.owner = THIS_MODULE pins the module.
	 */
}

module_init(demo_heap_init);
module_exit(demo_heap_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Demo dma-heap driver using alloc_page");
MODULE_AUTHOR("demo");
MODULE_IMPORT_NS(DMA_BUF);
