/*
 *  RT-Thread Priority Queue
 *
 * COPYRIGHT (C) 2013, Shanghai Real-Thread Technology Co., Ltd
 *
 *  This file is part of RT-Thread (http://www.rt-thread.org)
 *
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2013-09-11     Grissiom     the first verion
 */

#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "prio_queue.h"

#define QUE_ITEM_SZ 64

struct rt_prio_queue_item {
    struct rt_prio_queue_item *next;
    /* data follows */
};

static void _do_push(struct rt_prio_queue *que,
                     unsigned char prio,
                     struct rt_prio_queue_item *item)
{
	spin_lock(&que->lock);
	if (que->head[prio] == NULL)
	{
		que->head[prio] = item;
		que->bitmap |= 1 << prio;
	}
	else
	{
		BUG_ON(!(que->tail[prio]));
		que->tail[prio]->next = item;
	}
	que->tail[prio] = item;
	spin_unlock(&que->lock);
}

static struct rt_prio_queue_item* _do_pop(struct rt_prio_queue *que)
{
	int ffs;
	struct rt_prio_queue_item *item;

	ffs = __builtin_ffs(que->bitmap);
	if (ffs == 0)
		return NULL;
	ffs--;

	spin_lock(&que->lock);
	item = que->head[ffs];
	BUG_ON(!(item));

	que->head[ffs] = item->next;
	if (que->head[ffs] == NULL)
	{
		que->bitmap &= ~(1 << ffs);
	}
	spin_unlock(&que->lock);

	return item;
}

struct rt_prio_queue* rt_prio_queue_create(const char *name,
					   size_t item_nr,
                                           size_t item_sz)
{
	struct rt_prio_queue *que;

	que = kzalloc(sizeof(*que), GFP_KERNEL);
	if (!que)
		return NULL;
	que->pool = kmem_cache_create(name, item_sz, 0,
				      SLAB_HWCACHE_ALIGN, NULL);
	if (!que->pool) {
		kfree(que);
		return NULL;
	}

	que->item_sz = item_sz;
	init_waitqueue_head(&que->pop_wait);
	init_waitqueue_head(&que->push_wait);
	atomic_set(&que->item_avaialble, item_nr);
	spin_lock_init(&que->lock);

	return que;
}

/** Delete the queue.
 *
 * It could be dangrous and error prone because other process may waiting for
 * push/pop on it. If we delete it, they may crash. If you are confident about
 * the situation, you can delete the queue any way.
 */
void rt_prio_queue_delete(struct rt_prio_queue *que)
{
	struct rt_prio_queue_item *item;

	BUG_ON(waitqueue_active(&que->pop_wait) ||
	       waitqueue_active(&que->push_wait));

	/* clean up mem cache */
	for (item = _do_pop(que); item; item = _do_pop(que)) {
		kmem_cache_free(que->pool, item);
	}
	kmem_cache_destroy(que->pool);

	/* God bless the processes want to hold the que again. */
	kfree(que);
}

/**
 * return 0 on OK.
 */
int rt_prio_queue_push(struct rt_prio_queue *que,
		       unsigned char prio,
		       char *data)
{
	int res = 0;
	struct rt_prio_queue_item *item;

	if (atomic_dec_return(&que->item_avaialble) < 0) {
		DEFINE_WAIT(__wait);

		atomic_inc(&que->item_avaialble);

		for (;;) {
			prepare_to_wait(&que->push_wait, &__wait, TASK_INTERRUPTIBLE);

			if (atomic_dec_return(&que->item_avaialble) >= 0)
				break;
			atomic_inc(&que->item_avaialble);

			if (signal_pending(current)) {
				finish_wait(&que->push_wait, &__wait);
				res = -ERESTARTSYS;
				goto Out;
			}

			schedule();
		}
		finish_wait(&que->push_wait, &__wait);
	}

	item = kmem_cache_alloc(que->pool, GFP_KERNEL);
	if (!item) {
		res = -ENOMEM;
		goto Out;
	}

	item->next = NULL;
	memcpy(item+1, data, que->item_sz);

	_do_push(que, prio, item);

	wake_up_interruptible(&que->pop_wait);

Out:
	return res;
}

int rt_prio_queue_pop(struct rt_prio_queue *que,
		      char *data)
{
	int res;
	struct rt_prio_queue_item *item;

	res = wait_event_interruptible(que->pop_wait,
				       (item = _do_pop(que)));
	if (res)
		goto Out;

	memcpy(data, item+1, que->item_sz);
	kmem_cache_free(que->pool, item);

	atomic_inc(&que->item_avaialble);
	wake_up_interruptible(&que->push_wait);

Out:
	return res;
}

int rt_prio_queue_trypop(struct rt_prio_queue *que,
			 char *data)
{
	struct rt_prio_queue_item *item;

	item = _do_pop(que);
	if (!item)
		return -1;

	memcpy(data, item+1, que->item_sz);
	kmem_cache_free(que->pool, item);

	atomic_inc(&que->item_avaialble);
	wake_up_interruptible(&que->push_wait);

	return 0;
}
