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

#ifndef __PRIO_QUEUE_H__
#define __PRIO_QUEUE_H__

#define RT_PRIO_QUEUE_PRIO_MAX  32

struct rt_prio_queue_item;

struct rt_prio_queue {
    u32 bitmap;
    struct rt_prio_queue_item *head[RT_PRIO_QUEUE_PRIO_MAX];
    struct rt_prio_queue_item *tail[RT_PRIO_QUEUE_PRIO_MAX];

    size_t item_sz;

    wait_queue_head_t pop_wait, push_wait;
    struct kmem_cache *pool;

    atomic_t item_avaialble;
    spinlock_t lock;
};

struct rt_prio_queue* rt_prio_queue_create(const char *name,
					   size_t item_nr,
                                           size_t item_sz);
void rt_prio_queue_delete(struct rt_prio_queue *que);
int rt_prio_queue_push(struct rt_prio_queue *que,
		       unsigned char prio,
		       char *data);
int rt_prio_queue_pop(struct rt_prio_queue *que,
		      char *data);
int rt_prio_queue_trypop(struct rt_prio_queue *que,
			 char *data);

#endif /* end of include guard: __PRIO_QUEUE_H__ */
