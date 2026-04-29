// SPDX-License-Identifier: GPL-2.0
/*
 * demo_timer_fence.c - Timer-based dma_fence implementation.
 *
 * When the timer fires, a workqueue handler signals the fence.
 * The actual buffer fill is done synchronously in the ioctl handler
 * BEFORE the fence is created, to avoid Linux 6.1's deadlock where
 * both dma_buf_begin_cpu_access() and dma_buf_vmap() internally
 * call dma_resv_wait_timeout(WRITE).
 *
 * Flow:
 *   1. ioctl waits for prior WRITE fences
 *   2. ioctl calls dma_buf_begin_cpu_access + dma_buf_vmap + memset
 *      + dma_buf_vunmap + dma_buf_end_cpu_access
 *   3. ioctl creates a timer fence (no vmap ownership)
 *   4. ioctl adds fence to reservation object
 *   5. timer fires → work handler signals fence
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dma-fence.h>
#include <linux/dma-buf.h>
#include <linux/hrtimer.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/dma-resv.h>

/* ------------------------------------------------------------------ */
/* Data structure                                                      */
/* ------------------------------------------------------------------ */

struct demo_timer_fence {
	struct dma_fence base;
	spinlock_t lock;
	struct hrtimer timer;
	struct work_struct work;
};

static inline struct demo_timer_fence *to_demo_timer_fence(struct dma_fence *f)
{
	return container_of(f, struct demo_timer_fence, base);
}

/* ------------------------------------------------------------------ */
/* dma_fence_ops                                                       */
/* ------------------------------------------------------------------ */

static const char *demo_timer_fence_get_driver_name(struct dma_fence *fence)
{
	return "demo_timer";
}

static const char *demo_timer_fence_get_timeline_name(struct dma_fence *fence)
{
	return "demo_timer";
}

static bool demo_timer_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void demo_timer_fence_release(struct dma_fence *fence)
{
	struct demo_timer_fence *tf = to_demo_timer_fence(fence);
	kfree(tf);
}

static const struct dma_fence_ops demo_timer_fence_ops = {
	.get_driver_name	= demo_timer_fence_get_driver_name,
	.get_timeline_name	= demo_timer_fence_get_timeline_name,
	.enable_signaling	= demo_timer_fence_enable_signaling,
	.wait			= dma_fence_default_wait,
	.release		= demo_timer_fence_release,
};

/* ------------------------------------------------------------------ */
/* Workqueue handler: signal fence                                    */
/* ------------------------------------------------------------------ */

static void demo_fill_work(struct work_struct *work)
{
	struct demo_timer_fence *tf =
		container_of(work, struct demo_timer_fence, work);

	pr_info("demo_fill_work: signaling fence\n");
	dma_fence_signal(&tf->base);
	dma_fence_put(&tf->base);	/* drop work-handler ref */
}

/* ------------------------------------------------------------------ */
/* hrtimer callback: schedule signal work                              */
/* ------------------------------------------------------------------ */

static enum hrtimer_restart demo_timer_cb(struct hrtimer *timer)
{
	struct demo_timer_fence *tf =
		container_of(timer, struct demo_timer_fence, timer);

	dma_fence_get(&tf->base);	/* ref for work handler */
	schedule_work(&tf->work);

	return HRTIMER_NORESTART;
}

/* ------------------------------------------------------------------ */
/* Factory                                                             */
/* ------------------------------------------------------------------ */

/**
 * demo_timer_fence_create() - Create a timer-based fence.
 * @delay_ms:     Delay in milliseconds before signaling.
 *
 * The fence carries no buffer reference or vmap state — the caller
 * must fill the buffer before calling this function.
 */
struct dma_fence *demo_timer_fence_create(unsigned int delay_ms)
{
	struct demo_timer_fence *tf;

	tf = kzalloc(sizeof(*tf), GFP_KERNEL);
	if (!tf)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&tf->lock);
	INIT_WORK(&tf->work, demo_fill_work);

	dma_fence_init(&tf->base, &demo_timer_fence_ops, &tf->lock,
		       dma_fence_context_alloc(1), 1);

	hrtimer_init(&tf->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	tf->timer.function = demo_timer_cb;

	hrtimer_start(&tf->timer,
		      ms_to_ktime(delay_ms),
		      HRTIMER_MODE_REL);

	return &tf->base;
}
EXPORT_SYMBOL_GPL(demo_timer_fence_create);
