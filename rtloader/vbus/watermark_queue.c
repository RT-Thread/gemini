/*
 * Thread queue with water mark
 *
 * COPYRIGHT (C) 2014, Shanghai Real-Thread Technology Co., Ltd
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
 * 2014-05-19     Grissiom     first version
 */

#include <linux/sched.h>
#include <linux/wait.h>

#include "watermark_queue.h"

void rt_wm_que_set_mark(struct rt_watermark_queue *wg,
			unsigned int low, unsigned int high)
{
    BUG_ON(low > high);

    wg->high_mark = high;
    wg->low_mark = low;
}

void rt_wm_que_init(struct rt_watermark_queue *wg,
		    unsigned int low, unsigned int high)
{
    rt_wm_que_set_mark(wg, low, high);
    init_waitqueue_head(&wg->waitq);
    wg->level = 0;
    spin_lock_init(&wg->lock);
}

void rt_wm_que_dump(struct rt_watermark_queue *wg)
{
    pr_info("wg %p: low: %d, high: %d, cur: %d\n",
               wg, wg->low_mark, wg->high_mark, wg->level);
}
